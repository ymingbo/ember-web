/*
 * ------------------------------------------------------------------------
 * 名称：apu-2a03.hpp
 * 说明：经典 FC 的 APU（2A03 片上）：两个方波、三角波、噪声、DMC，帧计数器，非线性混音，输出单声道 PCM。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * 备注：寄存器 $4000-$4013、$4015 挂在 CPU 总线上；$4017 的帧计数器部分由本类的 write 处理（$4017 同时属于手柄口，
 *       由主机装配里的分发设备把写入转给两边，本类与手柄口模块互不引用）。
 *       按调度器的方式运行：不被逐周期调用，在被访问前与自己的事件到期时追赶到当前时间（内部逐周期推进）；
 *       会改变外部可见状态的时刻（帧 IRQ 置位、帧计数器的写入生效、DMC 取样请求）都预测出来登记为调度器事件，
 *       所以 CPU 逐周期采样 IRQ 线时不会漏掉变化。
 *       时间约定：APU 周期 T（从 1 起）发生在 CPU 周期 T-1 的中点（主时钟 12(T-1)+6）；CPU 在周期 c 访问寄存器时，APU 已推进到周期 c+1。
 *       帧计数器的步进表、$4017 写入的 3/4 周期延迟、长度计数器的延迟载入与 DMC 的启动延迟按真机的实测结果实现。
 *       DMC 的取样 DMA 按“请求后暂停 CPU 4 个周期、在第 4 个周期读采样”近似，见 docs/02-硬件模块.md 的已知差异。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "contracts/hardware/clock.hpp"
#include "contracts/hardware/cpu-bus.hpp"
#include "contracts/hardware/device.hpp"
#include "contracts/hardware/irq-line.hpp"
#include "contracts/hardware/rdy-line.hpp"
#include "contracts/hardware/scheduler.hpp"
#include "logic/hardware/apu-2a03/apu-channels.hpp"
#include "logic/hardware/apu-2a03/apu-tables.hpp"
#include "shared/audio/band-limited-sampler.hpp"

namespace ember::logic::hardware {

/** 经典 FC 的 APU。 */
class Apu2a03 final : public contracts::hardware::Device {
 public:
  /** 输出的采样率（赫兹）。 */
  static constexpr unsigned SAMPLE_RATE = 48000;

  /**
   * @param bus CPU 总线：DMC 的取样 DMA 从它读采样。
   * @param scheduler 调度器：登记预测的事件。
   * @param irq CPU 的 IRQ 线，本对象在其上登记帧 IRQ 与 DMC IRQ 两个来源。
   * @param rdy CPU 的 RDY 线，本对象在其上登记一个来源，DMC 取样时暂停 CPU。
   * @param cpuDomain CPU 的时钟域。
   * @param cpuFrequency CPU 的时钟频率（赫兹），用于把周期换算成输出采样。
   * @param pal 为 true 时噪声、DMC 的周期表与帧计数器的步进表用 PAL 的（默认 NTSC）。
   */
  Apu2a03(contracts::hardware::CpuBus& bus, contracts::hardware::Scheduler& scheduler, contracts::hardware::IrqLine& irq, contracts::hardware::RdyLine& rdy, contracts::hardware::ClockDomain cpuDomain, double cpuFrequency, bool pal = false);

  /** 读：只有 $4015 驱动数据线（位 5 保持总线上的值），其他地址不驱动。读 $4015 清除帧 IRQ 标志。 */
  contracts::hardware::BusRead read(std::uint16_t address) override;

  /** 写：$4000-$4013、$4015，以及 $4017 的帧计数器部分；其他地址忽略。 */
  void write(std::uint16_t address, std::uint8_t value) override;

  /**
   * 复位。上电：全部清零，帧计数器的行为如同在复位序列开始时写入 $00；
   * 软复位：相当于写 $4015 为 0（关闭通道、清长度计数器），帧计数器按上次写入 $4017 的值重新开始，三角波的暂停位保持。
   */
  void reset(bool power) override;

  void onEvent(contracts::hardware::MasterTime when, std::uint32_t tag) override;

  /** 追赶到主时钟的 now：推进内部状态并生成这段时间的采样。 */
  void catchUp(contracts::hardware::MasterTime now);

  /** 停止：取消调度事件，释放 IRQ 线与 RDY 线上的来源；主机型号不用这个 APU 时调用，再 reset 就恢复运行。 */
  void halt();

  /** 取走自上次以来生成的采样（单声道，范围 -1 到 1，采样率 SAMPLE_RATE）；先清空 samples 再放入。 */
  void takeAudio(std::vector<float>& samples);

  // 测试与调试用
  /** 已推进到的 APU 周期。 */
  [[nodiscard]] std::int64_t cycle() const { return cycle_; }
  [[nodiscard]] bool frameIrq() const { return frameIrq_; }
  [[nodiscard]] bool dmcIrq() const { return dmc_.irqFlag(); }
  /** 当前混音电平（0 到约 1，未经带限与直流阻断）。 */
  [[nodiscard]] double mixLevel() const { return level_; }

 private:
  static constexpr std::uint32_t TAG_WAKE = 0;
  static constexpr std::uint32_t TAG_DMC_READ = 1;
  static constexpr std::uint32_t TAG_DMC_RELEASE = 2;

  void stepCycle();
  void stepFrameCounter();
  void clockFrame(apu::FrameClock clock);
  void writeStatus(std::uint8_t value);
  void writeFrameCounter(std::uint8_t value);
  void startDmcDma();
  void updateOutput();
  [[nodiscard]] double computeLevel(unsigned& key) const;
  void syncIrq();
  void scheduleWake();
  void trimPending();
  [[nodiscard]] contracts::hardware::MasterTime cycleTime(std::int64_t cycle) const;

  contracts::hardware::CpuBus& bus_;
  contracts::hardware::Scheduler& scheduler_;
  contracts::hardware::IrqLine& irq_;
  contracts::hardware::RdyLine& rdy_;
  contracts::hardware::SignalSource frameIrqSource_;
  contracts::hardware::SignalSource dmcIrqSource_;
  contracts::hardware::SignalSource rdySource_;
  contracts::hardware::ClockDomain cpuDomain_;

  apu::PulseChannel pulse1_{true};
  apu::PulseChannel pulse2_{false};
  apu::TriangleChannel triangle_;
  apu::NoiseChannel noise_;
  apu::DmcChannel dmc_;

  std::int64_t cycle_{0};

  // 帧计数器
  const std::array<std::array<std::uint32_t, apu::FRAME_STEP_COUNT>, 2>* stepCycles_;
  unsigned frameMode_{0};
  unsigned frameStep_{0};
  std::uint32_t frameCycles_{0};
  bool irqInhibit_{false};
  bool frameIrq_{false};
  std::uint8_t pendingFrameValue_{0};
  int pendingFrameDelay_{0};
  int blockFrameClock_{0};

  // 混音与采样
  shared::BandLimitedSampler sampler_;
  std::vector<float> samples_;
  double level_{0};
  unsigned lastKey_{0};
  std::array<double, 31> pulseTable_{};

  contracts::hardware::EventId wakeEvent_{contracts::hardware::NO_EVENT};
  contracts::hardware::EventId dmaReadEvent_{contracts::hardware::NO_EVENT};
  contracts::hardware::EventId dmaReleaseEvent_{contracts::hardware::NO_EVENT};
};

}  // namespace ember::logic::hardware
