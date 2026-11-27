/*
 * ------------------------------------------------------------------------
 * 名称：tape-deck.hpp
 * 说明：磁带机：播放时把磁带的采样流按主时钟换成数据线上的 1 位信号与声音，录音时把主机的声音与主机送来的 1 位信号按时间记成采样。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-15
 * 备注：播放的时间都是模拟时间：位置 = 播放起点的帧号 + 经过的主时钟周期按 96 kHz 换算出的帧数；读数据线、读位置、取声音时才把磁带处理到现在，
 *       所以没有人读时不占调度；设置了边沿通知时，向前扫描找到下一次翻转，在翻转的时刻登记一个调度器事件，事件里再回调；
 *       数据线取立体声磁带的右声道（单声道磁带的唯一声道），经施密特比较器转成 1 位信号；声音取左声道（单声道磁带的唯一声道），降采样后混进机器的声音，
 *       不论主机型号有没有接数据线；
 *       录音记的是主机的声音（每次取声音时由 mixAudio 交来）加数据线：每次翻转前，把上一个电平按经过的时间补成 32 kHz 的方波采样（±20000），
 *       数据线只在主机第一次把它拉高之后才叠加（游戏每次写手柄口都会拉低它，不能让一路直流偏置进到录音里），再按时间对齐叠加主机的声音，交给录音去向；
 *       磁带放完后自动停止，再按播放时回到开头；定位（seek）清掉缓冲与声音，从新位置重新开始处理（播放时重新锚定时间并重新扫描边沿，数据线回到静音对应的状态）；
 *       磁带机是独立的设备，不属于主机：主机通电时把自己的调度器绑定给它（bind），断电或被替换时解绑（unbind，先停止，录音在这里写完）；
 *       没绑定时磁带仍然可以载入与取出，位置保持不变，只是不能播放与录音；来源可能阻塞（外部程序解码的管道），读取发生在模拟线程。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "contracts/hardware/scheduler.hpp"
#include "contracts/hardware/tape-deck.hpp"

namespace ember::logic::hardware {

/** 磁带机。 */
class TapeDeck final : public contracts::hardware::TapeControl, public contracts::hardware::TapeLine, public contracts::hardware::EventListener {
 public:
  /** 录音时一个电平的采样幅度。 */
  static constexpr std::int16_t RECORD_AMPLITUDE = 20000;
  /** 一次从来源取的帧数。 */
  static constexpr std::size_t CHUNK_FRAMES = 4096;
  /** 向前扫描翻转的最大距离（帧，1 秒）；超过时在扫描终点再登记一次扫描。 */
  static constexpr std::uint64_t SCAN_FRAMES = contracts::hardware::TapeSource::SAMPLE_RATE;

  /** 没有绑定调度器的磁带机：只能载入与取出磁带，等主机通电时绑定。 */
  TapeDeck() = default;
  /**
   * 创建后马上绑定的磁带机。
   * @param scheduler 调度器：取当前时间，登记边沿事件。
   * @param masterHz 主时钟频率（赫兹）。
   */
  TapeDeck(contracts::hardware::Scheduler& scheduler, double masterHz);
  ~TapeDeck() override;

  TapeDeck(const TapeDeck&) = delete;
  TapeDeck& operator=(const TapeDeck&) = delete;

  /**
   * 接到通电的主机上：先在原来的调度器上停止，再换成新的；磁带与位置不变。
   * @param scheduler 主机的调度器。
   * @param masterHz 主时钟频率（赫兹）。
   */
  void bind(contracts::hardware::Scheduler& scheduler, double masterHz);
  /** 断开：停止（录音在这里写完）并解除绑定；没绑定时没有效果。 */
  void unbind();
  /** 只在当前绑定的就是 scheduler 时断开：主机被替换时，旧主机不会把新主机的绑定解除。 */
  void unbind(const contracts::hardware::Scheduler& scheduler);

  // TapeControl
  [[nodiscard]] bool loaded() const override { return source_ != nullptr || recording(); }
  [[nodiscard]] bool connected() const override { return scheduler_ != nullptr; }
  [[nodiscard]] std::string name() const override { return name_; }
  [[nodiscard]] contracts::hardware::TapeMode mode() override;
  [[nodiscard]] double positionSeconds() override;
  [[nodiscard]] double durationSeconds() const override { return source_ != nullptr ? source_->durationSeconds() : 0.0; }
  [[nodiscard]] bool stereo() const override { return source_ != nullptr && source_->stereo(); }
  void load(std::unique_ptr<contracts::hardware::TapeSource> source, std::string name) override;
  void play() override;
  void seek(double seconds) override;
  void record(std::unique_ptr<contracts::hardware::TapeSink> sink, std::string name, double startSeconds = 0.0) override;
  void stop() override;
  void eject() override;

  // TapeLine
  [[nodiscard]] bool level() override;
  void drive(bool high) override;
  void setComparator(const contracts::hardware::TapeComparator& comparator) override;
  void setEdgeListener(std::function<void(bool level, std::uint32_t samplesSincePrevious)> listener) override;

  // EventListener
  void onEvent(contracts::hardware::MasterTime when, std::uint32_t tag) override;

  /** 设置声音的输出采样率（赫兹）；0 表示不产生声音。 */
  void setAudioRate(unsigned rate);

  /**
   * 机器的声音经过磁带机：播放时把磁带的声音加进 samples；多出的留到下次。录音时把 samples（机器完整的声音）记进磁带。
   * 机器每次取走声音时调用，samples 的采样率是 setAudioRate 设置的采样率。
   */
  void mixAudio(std::vector<float>& samples);

 private:
  using Frame = std::array<std::int16_t, 2>;

  /** 事件标记：数据线翻转、继续扫描、磁带放完。 */
  enum Tag : std::uint32_t { TagEdge = 0, TagRescan = 1, TagEnd = 2 };

  [[nodiscard]] bool recording() const { return mode_ == contracts::hardware::TapeMode::Recording; }

  /** 当前时间；没有绑定调度器时为 0（此时不会在播放或录音，用不到它）。 */
  [[nodiscard]] contracts::hardware::MasterTime now() const { return scheduler_ == nullptr ? 0 : scheduler_->now(); }

  /** 播放中当前时刻对应的帧号（正在放的那一帧）。 */
  [[nodiscard]] std::uint64_t frameAt(contracts::hardware::MasterTime now) const;
  /** 某一帧对应的主时钟时间（向上取整，保证到点时 frameAt 不小于它）。 */
  [[nodiscard]] contracts::hardware::MasterTime tickOf(std::uint64_t frame) const;
  /** 把播放处理到现在。 */
  void advanceTo(contracts::hardware::MasterTime now);
  /** 保证缓冲里有这一帧；来源已经放完时返回 false。 */
  bool ensureBuffered(std::uint64_t frame);
  /** 处理一帧：比较器与声音。 */
  void processFrame(const Frame& frame);
  /** 施密特比较器：给出新的内部状态。 */
  [[nodiscard]] bool schmitt(bool state, std::int16_t sample) const;
  /** 磁带放完：停止，位置停在结尾。 */
  void finishPlayback();
  /** 清掉缓冲，回到开头（来源倒带）。 */
  void rewindSource();
  /** 重新向前扫描下一次翻转并登记事件。 */
  void scheduleNextEdge();
  void cancelEvent();
  /** 录音：把上一个电平补到现在。 */
  void renderRecording(contracts::hardware::MasterTime now);
  /** 录音：把 samples 叠加到还没有混入声音的数据线采样上。 */
  void mixRecordedAudio(const std::vector<float>& samples);
  /** 把已经混好的采样交给去向；all 为真时连没有混入声音的也一起交出。 */
  void flushRecording(bool all);

  /** 主机的调度器；没有接在通电的主机上时为空。 */
  contracts::hardware::Scheduler* scheduler_{nullptr};
  /** 每帧（96 kHz）占的主时钟周期数。 */
  double ticksPerFrame_{1};
  double masterHz_{1};

  std::unique_ptr<contracts::hardware::TapeSource> source_;
  std::unique_ptr<contracts::hardware::TapeSink> sink_;
  std::string name_;
  contracts::hardware::TapeMode mode_{contracts::hardware::TapeMode::Stopped};

  // 播放
  double anchorTick_{0};
  std::uint64_t anchorFrame_{0};
  /** 下一个要处理的帧号。 */
  std::uint64_t processed_{0};
  std::deque<Frame> buffer_;
  /** buffer_ 第一帧的帧号。 */
  std::uint64_t bufferBase_{0};
  bool endOfSource_{false};
  /** 来源放完时的总帧数。 */
  std::uint64_t endFrame_{0};
  contracts::hardware::TapeComparator comparator_;
  bool schmittState_{false};

  // 声音
  unsigned audioRate_{0};
  double framesPerAudioSample_{0};
  double audioPhase_{0};
  double audioSum_{0};
  double audioCount_{0};
  std::vector<float> audioPending_;

  // 边沿
  std::function<void(bool, std::uint32_t)> listener_;
  contracts::hardware::EventId eventId_{contracts::hardware::NO_EVENT};
  std::uint64_t scanFrame_{0};
  bool scanState_{false};
  std::uint64_t pendingEdgeFrame_{0};
  bool pendingEdgeLevel_{false};
  std::uint64_t lastEdgeFrame_{0};

  // 录音
  bool driven_{false};
  double recordStartTick_{0};
  /** 录音开始处在磁带上的位置（秒）。 */
  double recordStartSeconds_{0};
  std::uint64_t recordedSamples_{0};
  bool recordLevel_{false};
  /** 录音以来主机是否拉高过数据线：没有拉高过时数据线不叠加。 */
  bool recordLineUsed_{false};
  /** 已按数据线渲染、等待交给去向的采样；前 recordMixed_ 个已经叠加了机器的声音。 */
  std::vector<std::int16_t> recordBuffer_;
  std::size_t recordMixed_{0};
};

}  // namespace ember::logic::hardware
