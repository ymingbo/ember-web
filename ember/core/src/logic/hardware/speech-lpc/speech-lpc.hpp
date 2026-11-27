/*
 * ------------------------------------------------------------------------
 * 名称：speech-lpc.hpp
 * 说明：学习机的语音芯片（$4302）：16 字节的接收缓冲加 LPC-10 合成器，把 BIOS 送来的语音码流还原成 10 kHz 的单声道声音。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-15
 * 备注：寄存器：读 $4302，位 7 为 1 表示缓冲未满、可以写下一个字节，低 4 位为 $F 表示播放结束；写 $4302 送入一个码流字节；
 *       扩展 I/O 位 6 的上升沿复位本模块（reset）；
 *       码流：第 1 个字节是命令，$0A 选 LPC-10 D6 码表，$0B 选 LPC-10 PE 码表，其他字节丢弃；之后按帧读取，每帧先读 4 位能量
 *       （0 是静音帧，15 是结束帧），其余各项取决于码表；字节按位反序使用（先取最低位）；每帧 200 个采样（20 毫秒，10 kHz）；
 *       结束帧读到后“播放结束”置位，此后收到字节 $0F 才重新开始（清空缓冲，等待下一个命令字节），其他字节被丢弃；
 *       合成按主时钟推进：每个采样一个调度器事件，只有在播放时才登记事件，空闲时不占调度；一帧所需的字节不够时输出静音并等待；
 *       输出电平经 onLevel 回调交给主机装配里的 PSG 混音；
 *       缓冲写满时再写入的字节被丢弃（程序应先读状态位 7）；
 *       裕兴变体：没有命令字节，上电与复位后直接按 PE 码表读帧；读到结束帧后丢掉当前字节的剩余位，紧接着开始读下一段（没有确认字节）；读状态时缓冲里不到 8 个字节读 $00（可以写），否则读 $80（忙）。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>

#include "contracts/hardware/device.hpp"
#include "contracts/hardware/scheduler.hpp"

namespace ember::logic::hardware {

/** 学习机的语音芯片。 */
class SpeechLpc final : public contracts::hardware::Device {
 public:
  /**
   * 芯片的接线变体：
   * Subor：命令字节 $0A（D6 码表）或 $0B（PE 码表），低 4 位为 $F 表示播放结束，播放结束后收到 $0F 才重新开始；
   * Bbg：命令字节 $D6（只有 D6 码表），状态低 4 位恒为 $F，读到结束帧后停止，只有复位才重新开始；
   * Yuxing：没有命令字节，只有 PE 码表，读到结束帧后直接开始下一段；状态位 7 为 1 表示缓冲已有 8 个字节以上（忙）。
   */
  enum class Variant : std::uint8_t { Subor, Bbg, Yuxing };

  /** 输出采样率（赫兹）。 */
  static constexpr unsigned SAMPLE_RATE = 10000;
  /** 每帧的采样数。 */
  static constexpr unsigned SAMPLES_PER_FRAME = 200;
  /** 接收缓冲的容量（字节）；缓冲是 16 项的环形缓冲，满的判据是留一项，所以实际能放 15 个。 */
  static constexpr std::size_t FIFO_CAPACITY = 15;
  /** 裕兴变体：缓冲里达到这么多字节就报忙。 */
  static constexpr std::size_t YUXING_BUSY_BYTES = 8;
  /** LPC 的阶数。 */
  static constexpr unsigned ORDER = 10;

  /**
   * @param scheduler 调度器，每个采样登记一个事件。
   * @param masterHz 主时钟频率（赫兹），用来换算采样间隔。
   * @param variant 接线变体，默认 Subor。
   */
  SpeechLpc(contracts::hardware::Scheduler& scheduler, double masterHz, Variant variant = Variant::Subor);

  /** 读状态：位 7 缓冲未满，低 4 位为 $F 表示播放结束（裕兴变体：位 7 为 1 表示忙，没有播放结束位）。 */
  contracts::hardware::BusRead read(std::uint16_t address) override;
  contracts::hardware::BusRead peek(std::uint16_t address) override { return read(address); }
  /** 写一个码流字节。 */
  void write(std::uint16_t address, std::uint8_t value) override;
  /** 复位：清空缓冲，回到等待命令字节，播放结束标志清除，输出静音。 */
  void reset(bool power) override;
  void onEvent(contracts::hardware::MasterTime when, std::uint32_t tag) override;

  /** 播放结束（读 $4302 低 4 位为 $F）。 */
  [[nodiscard]] bool ended() const { return ended_; }
  /** 缓冲里等待合成的字节数。 */
  [[nodiscard]] std::size_t pendingBytes() const { return count_; }
  /** 是否正在逐采样合成（有事件登记）。 */
  [[nodiscard]] bool playing() const { return state_ == State::Run; }
  /** 已经完整合成的帧数（自上次复位），供测试。 */
  [[nodiscard]] std::uint64_t framesSynthesized() const { return framesSynthesized_; }

  /** 当前输出电平变化（-1 到 1）。 */
  std::function<void(double)> onLevel;

 private:
  enum class State : std::uint8_t {
    /** 等待命令字节。 */
    Startup,
    /** 命令字节已收到，读开头的两帧。 */
    Preload,
    /** 逐采样合成。 */
    Run,
    /** 读到结束帧，等待确认字节 $0F。 */
    Finished
  };

  /** 一帧的参数，按“能量、音高、K1-K10”的顺序排列（插值时当作 12 个 16 位整数处理）。 */
  struct Frame {
    std::int16_t energy{0};
    std::int16_t pitch{0};
    std::array<std::int16_t, ORDER> k{};
  };

  enum class FrameResult : std::uint8_t { Decoded, EndOfStream, NeedMoreData };

  /** 不需要时间的状态转移：消耗命令字节、读开头的帧、读下一帧、等待确认字节。直到需要等待数据或进入逐采样合成为止。 */
  void settle();
  /** 清空合成器内部状态（滤波器、插值、随机数种子、位缓存）。 */
  void restartSynth();
  /** 登记下一个采样的事件（已登记则什么也不做）。 */
  void scheduleTick();
  /** 在缓冲的拷贝上读一帧；成功才真正取走用到的字节。 */
  FrameResult tryReadFrame(Frame& destination, const Frame& reference);
  /** 缓冲里第 index 个字节。 */
  [[nodiscard]] std::uint8_t fifoAt(std::size_t index) const { return fifo_[(head_ + index) % fifo_.size()]; }
  void fifoPop();
  /** 合成一个采样，结果放在 output_。 */
  void synthesizeSample();
  /** 重新装入音高周期（按需要插值各参数）。 */
  std::int16_t reloadPitch();
  /** 设置帧间是否插值。 */
  void updateInterpolation();
  /** 格型滤波器：以 output_ 里的激励为输入，结果写回 output_。 */
  void runFilter();
  /** 读到结束帧：播放结束置位，等待确认字节（裕兴变体不置位，直接开始读下一段）。 */
  void finishStream();
  /** 伪随机的清音激励位。 */
  bool nextRandomBit();
  /** 输出电平变化时才通知。 */
  void emitLevel(double level);

  contracts::hardware::Scheduler& scheduler_;
  Variant variant_;
  /** 一个采样占的主时钟周期数（小数）。 */
  double ticksPerSample_;
  double nextTickAt_{0};
  contracts::hardware::EventId eventId_{contracts::hardware::NO_EVENT};

  // 接收缓冲（环形，容量 FIFO_CAPACITY，数组多留一项以便判满）
  std::array<std::uint8_t, FIFO_CAPACITY + 1> fifo_{};
  std::size_t head_{0};
  std::size_t count_{0};

  State state_{State::Startup};
  bool ended_{false};
  /** 用 D6 码表（命令字节 $0A）；否则用 PE 码表（$0B）。 */
  bool useD6Tables_{true};
  /** 位缓存：低 bitsLeft_ 位有效，高位在前。 */
  std::uint32_t bitCache_{0};
  unsigned bitsLeft_{0};
  /** Preload 阶段已经读好的帧数。 */
  unsigned preloaded_{0};
  /** 一帧合成完，等下一帧的数据。 */
  bool waitingNextFrame_{false};

  Frame previous_{};
  Frame current_{};
  Frame next_{};
  bool needInterpolation_{false};
  std::int16_t randomSeed_{-1};
  std::int16_t currentPitch_{0};
  unsigned sampleIndex_{0};
  std::array<std::int16_t, ORDER + 1> filterState_{};
  std::int16_t output_{0};
  double lastLevel_{0};
  std::uint64_t framesSynthesized_{0};
};

}  // namespace ember::logic::hardware
