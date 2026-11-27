/*
 * ------------------------------------------------------------------------
 * 名称：apu-2a03.cpp
 * 说明：经典 FC APU 的实现：寄存器、帧计数器、DMC 取样 DMA、非线性混音。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * ------------------------------------------------------------------------
 */

#include "logic/hardware/apu-2a03/apu-2a03.hpp"

#include <algorithm>
#include <limits>

#include "contracts/hardware/hardware-error.hpp"
#include "logic/hardware/apu-2a03/apu-tables.hpp"
#include "shared/errors/core-error.hpp"

namespace ember::logic::hardware {

namespace {

namespace hw = contracts::hardware;

/** 复位或上电后，帧计数器“写入 $4017”生效前的周期数（CPU 的复位序列 7 个周期，由 blargg 的 apu_reset/4017_timing 测试调定）。 */
constexpr int RESET_FRAME_WRITE_DELAY = 2;
/** $4017 的写入在偶数、奇数 APU 周期上生效前的延迟周期数。 */
constexpr int FRAME_WRITE_DELAY_EVEN = 3;
constexpr int FRAME_WRITE_DELAY_ODD = 4;
/** $4015 启用 DMC 后到请求取样的周期数（按写入的奇偶周期）。 */
constexpr unsigned DMC_START_DELAY_EVEN = 3;
constexpr unsigned DMC_START_DELAY_ODD = 2;
/** DMC 取样请求之后，CPU 被暂停的周期数；采样在最后一个暂停周期的中点读出。 */
constexpr int DMC_DMA_CYCLES = 4;
/** 没有人取走采样时最多保留的采样数。 */
constexpr std::size_t MAX_PENDING_SAMPLES = Apu2a03::SAMPLE_RATE;

/** 混音公式：三角波、噪声、DMC 共用的非线性项。 */
double tndLevel(unsigned triangle, unsigned noise, unsigned dmc) {
  const double sum = triangle / 8227.0 + noise / 12241.0 + dmc / 22638.0;
  return sum <= 0 ? 0.0 : 159.79 / (1.0 / sum + 100.0);
}

}  // namespace

Apu2a03::Apu2a03(hw::CpuBus& bus, hw::Scheduler& scheduler, hw::IrqLine& irq, hw::RdyLine& rdy, hw::ClockDomain cpuDomain, double cpuFrequency, bool pal)
    : bus_(bus),
      scheduler_(scheduler),
      irq_(irq),
      rdy_(rdy),
      frameIrqSource_(irq.addSource()),
      dmcIrqSource_(irq.addSource()),
      rdySource_(rdy.addSource()),
      cpuDomain_(cpuDomain),
      stepCycles_(pal ? &apu::FRAME_STEP_CYCLES_PAL : &apu::FRAME_STEP_CYCLES),
      sampler_(cpuFrequency, SAMPLE_RATE) {
  if (cpuDomain_.ticksPerCycle < 2) throw shared::CoreError(hw::ERROR_INVALID_ARGUMENT, "CPU 周期的主时钟周期数必须大于 1");
  noise_.setPal(pal);
  dmc_.setPal(pal);
  for (std::size_t count = 1; count < pulseTable_.size(); ++count) pulseTable_[count] = 95.88 / (8128.0 / static_cast<double>(count) + 100.0);
}

// 周期 c 的边界时刻：用“向上取整的半周期”，使 catchUp 里的“+ 半周期再取整”恰好得到 c（CPU 周期含奇数个主时钟周期时，向下取整会差 1，事件回调推不动进度而无限重登记）。
hw::MasterTime Apu2a03::cycleTime(std::int64_t cycle) const { return cpuDomain_.toMaster(cycle - 1) + (cpuDomain_.ticksPerCycle + 1) / 2; }

// ---------------------------------------------------------------- 总线

hw::BusRead Apu2a03::read(std::uint16_t address) {
  if (address != 0x4015) return hw::releaseBus();
  catchUp(scheduler_.now());
  std::uint8_t status = 0;
  if (pulse1_.length().active()) status |= 0x01;
  if (pulse2_.length().active()) status |= 0x02;
  if (triangle_.length().active()) status |= 0x04;
  if (noise_.length().active()) status |= 0x08;
  if (dmc_.active()) status |= 0x10;
  if (frameIrq_) status |= 0x40;
  if (dmc_.irqFlag()) status |= 0x80;
  // 读 $4015 清除帧 IRQ 标志，DMC 的标志不受影响。
  if (frameIrq_) {
    frameIrq_ = false;
    syncIrq();
  }
  // 位 5 不由 APU 驱动。
  return hw::driveBus(status, 0xDF);
}

void Apu2a03::write(std::uint16_t address, std::uint8_t value) {
  if ((address >= 0x4000 && address <= 0x4013) || address == 0x4015 || address == 0x4017) {
    catchUp(scheduler_.now());
  } else {
    return;
  }
  const unsigned reg = address & 3U;
  if (address <= 0x4003) {
    pulse1_.write(reg, value);
  } else if (address <= 0x4007) {
    pulse2_.write(reg, value);
  } else if (address <= 0x400B) {
    triangle_.write(reg, value);
  } else if (address <= 0x400F) {
    noise_.write(reg, value);
  } else if (address <= 0x4013) {
    dmc_.write(reg, value);
    syncIrq();
    scheduleWake();
  } else if (address == 0x4015) {
    writeStatus(value);
  } else {
    writeFrameCounter(value);
  }
}

void Apu2a03::writeStatus(std::uint8_t value) {
  pulse1_.length().setEnabled((value & 0x01U) != 0);
  pulse2_.length().setEnabled((value & 0x02U) != 0);
  triangle_.length().setEnabled((value & 0x04U) != 0);
  noise_.length().setEnabled((value & 0x08U) != 0);
  dmc_.setEnabled((value & 0x10U) != 0, (cycle_ & 1) != 0 ? DMC_START_DELAY_ODD : DMC_START_DELAY_EVEN);
  // 写 $4015 清除 DMC 的 IRQ 标志；帧 IRQ 标志不受影响。
  dmc_.clearIrq();
  syncIrq();
  scheduleWake();
}

void Apu2a03::writeFrameCounter(std::uint8_t value) {
  // 写入在 3 个周期后生效（写在奇数 APU 周期时是 4 个），生效时帧计数器从头开始。
  pendingFrameValue_ = value;
  pendingFrameDelay_ = (cycle_ & 1) != 0 ? FRAME_WRITE_DELAY_ODD : FRAME_WRITE_DELAY_EVEN;
  irqInhibit_ = (value & 0x40U) != 0;
  if (irqInhibit_ && frameIrq_) {
    frameIrq_ = false;
    syncIrq();
  }
  scheduleWake();
}

// ---------------------------------------------------------------- 复位

void Apu2a03::reset(bool power) {
  if (!power) catchUp(scheduler_.now());
  for (hw::EventId* event : {&wakeEvent_, &dmaReadEvent_, &dmaReleaseEvent_}) {
    if (*event != hw::NO_EVENT) scheduler_.cancel(*event);
    *event = hw::NO_EVENT;
  }
  rdy_.set(rdySource_, false);

  pulse1_.reset(power);
  pulse2_.reset(power);
  triangle_.reset(power);
  noise_.reset(power);
  dmc_.reset(power);

  frameIrq_ = false;
  syncIrq();
  if (power) {
    // 周期编号从当前的主时钟时间算起（主机上电时为 0；中途启动（如 sb2000 进入 FC 兼容模式）时不为 0）。
    cycle_ = (scheduler_.now() + cpuDomain_.ticksPerCycle / 2) / cpuDomain_.ticksPerCycle;
    frameMode_ = 0;
    irqInhibit_ = false;
    sampler_.reset(cycle_);
    samples_.clear();
  }
  frameStep_ = 0;
  frameCycles_ = 0;
  blockFrameClock_ = 0;
  // 上电时如同在复位序列开始时写入 $00，软复位时重新写入上次的值（模式不变，IRQ 禁止位保持）。
  pendingFrameValue_ = static_cast<std::uint8_t>((frameMode_ != 0 ? 0x80U : 0U) | (irqInhibit_ ? 0x40U : 0U));
  pendingFrameDelay_ = RESET_FRAME_WRITE_DELAY;

  if (power) {
    // 上电的初始电平不产生阶跃，避免开机的爆音。
    level_ = computeLevel(lastKey_);
  } else {
    updateOutput();
  }
  scheduleWake();
}

// ---------------------------------------------------------------- 追赶与逐周期推进

void Apu2a03::catchUp(hw::MasterTime now) {
  const std::int64_t target = (now + cpuDomain_.ticksPerCycle / 2) / cpuDomain_.ticksPerCycle;
  while (cycle_ < target) stepCycle();
  trimPending();
}

void Apu2a03::halt() {
  for (hw::EventId* event : {&wakeEvent_, &dmaReadEvent_, &dmaReleaseEvent_}) {
    if (*event != hw::NO_EVENT) scheduler_.cancel(*event);
    *event = hw::NO_EVENT;
  }
  irq_.set(frameIrqSource_, false);
  irq_.set(dmcIrqSource_, false);
  rdy_.set(rdySource_, false);
}

void Apu2a03::trimPending() {
  if (samples_.size() > MAX_PENDING_SAMPLES) samples_.erase(samples_.begin(), samples_.end() - static_cast<std::ptrdiff_t>(MAX_PENDING_SAMPLES));
}

void Apu2a03::takeAudio(std::vector<float>& samples) {
  samples.clear();
  samples.swap(samples_);
}

void Apu2a03::stepCycle() {
  ++cycle_;
  stepFrameCounter();
  pulse1_.endCycle();
  pulse2_.endCycle();
  triangle_.endCycle();
  noise_.endCycle();
  pulse1_.clockTimer();
  pulse2_.clockTimer();
  triangle_.clockTimer();
  noise_.clockTimer();
  dmc_.clock();
  if (dmc_.takeDmaRequest()) startDmcDma();
  updateOutput();
  sampler_.advance(cycle_, samples_);
}

void Apu2a03::clockFrame(apu::FrameClock clock) {
  // 四分之一帧：包络与三角波的线性计数器；半帧再加长度计数器与扫频。
  pulse1_.clockEnvelope();
  pulse2_.clockEnvelope();
  noise_.clockEnvelope();
  triangle_.clockLinear();
  if (clock == apu::FrameClock::Half) {
    pulse1_.clockLength();
    pulse2_.clockLength();
    triangle_.clockLength();
    noise_.clockLength();
    pulse1_.clockSweep();
    pulse2_.clockSweep();
  }
}

void Apu2a03::stepFrameCounter() {
  ++frameCycles_;
  if (frameCycles_ >= (*stepCycles_)[frameMode_][frameStep_]) {
    // 4 步模式的最后三个周期里每个周期都置位帧 IRQ 标志。
    if (!irqInhibit_ && frameMode_ == 0 && frameStep_ >= apu::FRAME_IRQ_FIRST_STEP && !frameIrq_) {
      frameIrq_ = true;
      syncIrq();
    }
    const apu::FrameClock clock = apu::FRAME_STEP_CLOCKS[frameMode_][frameStep_];
    if (clock != apu::FrameClock::None && blockFrameClock_ == 0) {
      clockFrame(clock);
      blockFrameClock_ = 2;
    }
    if (++frameStep_ == apu::FRAME_STEP_COUNT) {
      frameStep_ = 0;
      frameCycles_ = 0;
    }
  }
  if (pendingFrameDelay_ > 0 && --pendingFrameDelay_ == 0) {
    frameMode_ = (pendingFrameValue_ & 0x80U) != 0 ? 1U : 0U;
    frameStep_ = 0;
    frameCycles_ = 0;
    // 5 步模式在写入生效时立即时钟一次半帧与四分之一帧。
    if (frameMode_ == 1 && blockFrameClock_ == 0) {
      clockFrame(apu::FrameClock::Half);
      blockFrameClock_ = 2;
    }
  }
  if (blockFrameClock_ > 0) --blockFrameClock_;
}

// ---------------------------------------------------------------- 混音

double Apu2a03::computeLevel(unsigned& key) const {
  const unsigned pulse = static_cast<unsigned>(pulse1_.output()) + pulse2_.output();
  const unsigned triangle = triangle_.output();
  const unsigned noise = noise_.output();
  const unsigned dmc = dmc_.output();
  key = pulse | (triangle << 5) | (noise << 9) | (dmc << 13);
  return pulseTable_[pulse] + tndLevel(triangle, noise, dmc);
}

void Apu2a03::updateOutput() {
  unsigned key = 0;
  // 先用便宜的整数键判断有没有变化，没变化就不重算。
  const unsigned pulse = static_cast<unsigned>(pulse1_.output()) + pulse2_.output();
  key = pulse | (static_cast<unsigned>(triangle_.output()) << 5) | (static_cast<unsigned>(noise_.output()) << 9) | (static_cast<unsigned>(dmc_.output()) << 13);
  if (key == lastKey_) return;
  const double level = computeLevel(lastKey_);
  sampler_.addDelta(cycle_, level - level_);
  level_ = level;
}

// ---------------------------------------------------------------- 中断线与事件

void Apu2a03::syncIrq() {
  irq_.set(frameIrqSource_, frameIrq_);
  irq_.set(dmcIrqSource_, dmc_.irqFlag());
}

void Apu2a03::startDmcDma() {
  rdy_.set(rdySource_, true);
  dmaReadEvent_ = scheduler_.scheduleAt(cycleTime(cycle_) + cpuDomain_.toMaster(DMC_DMA_CYCLES), *this, TAG_DMC_READ);
}

void Apu2a03::scheduleWake() {
  if (wakeEvent_ != hw::NO_EVENT) scheduler_.cancel(wakeEvent_);
  wakeEvent_ = hw::NO_EVENT;

  constexpr std::int64_t NONE = std::numeric_limits<std::int64_t>::max();
  std::int64_t next = NONE;
  // 帧 IRQ 置位：4 步模式、未被禁止时，从第 3 步（含）起每一步都会置位。
  if (!irqInhibit_ && frameMode_ == 0) {
    const unsigned step = std::max(frameStep_, apu::FRAME_IRQ_FIRST_STEP);
    next = std::min(next, cycle_ + static_cast<std::int64_t>((*stepCycles_)[0][step]) - static_cast<std::int64_t>(frameCycles_));
  }
  // 写 $4017 生效：此后帧计数器重新开始，原先的预测作废。
  if (pendingFrameDelay_ > 0) next = std::min(next, cycle_ + pendingFrameDelay_);
  // DMC 取样请求。
  const std::int64_t dmc = dmc_.cyclesUntilRequest();
  if (dmc > 0) next = std::min(next, cycle_ + dmc);
  if (next != NONE) wakeEvent_ = scheduler_.scheduleAt(cycleTime(next), *this, TAG_WAKE);
}

void Apu2a03::onEvent(hw::MasterTime when, std::uint32_t tag) {
  switch (tag) {
    case TAG_WAKE:
      wakeEvent_ = hw::NO_EVENT;
      catchUp(when);
      scheduleWake();
      break;
    case TAG_DMC_READ: {
      dmaReadEvent_ = hw::NO_EVENT;
      catchUp(when);
      dmc_.deliverSample(bus_.read(dmc_.dmaAddress()));
      syncIrq();
      // 暂停在这个周期结束时释放。
      dmaReleaseEvent_ = scheduler_.scheduleAt(when + cpuDomain_.ticksPerCycle - cpuDomain_.ticksPerCycle / 2, *this, TAG_DMC_RELEASE);
      scheduleWake();
      break;
    }
    default:
      dmaReleaseEvent_ = hw::NO_EVENT;
      rdy_.set(rdySource_, false);
      break;
  }
}

}  // namespace ember::logic::hardware
