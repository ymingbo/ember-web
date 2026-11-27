/*
 * ------------------------------------------------------------------------
 * 名称：ppu-2c02.cpp
 * 说明：2C02 的寄存器读写、复位、时间追赶、NMI 线与事件调度、帧输出。渲染见 ppu-2c02-render.cpp。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * 备注：寄存器行为与真机一致，用 blargg 的 PPU 测试 ROM 验证。
 * ------------------------------------------------------------------------
 */

#include "logic/hardware/ppu-2c02/ppu-2c02.hpp"

#include <algorithm>
#include <bit>

#include "contracts/hardware/hardware-error.hpp"
#include "logic/hardware/ppu-2c02/ppu-palette.hpp"
#include "shared/errors/core-error.hpp"

namespace ember::logic::hardware {

namespace {

namespace hw = contracts::hardware;

// 帧缓冲的字节序按 RGBA 存放，核心只支持小端机器，见 contracts/hardware/ppu.hpp。
static_assert(std::endian::native == std::endian::little, "核心只支持小端机器");

constexpr std::uint8_t CTRL_NMI_ENABLE = 0x80;
constexpr std::uint8_t CTRL_INCREMENT_32 = 0x04;
constexpr std::uint8_t STATUS_VBLANK = 0x80;
constexpr std::uint8_t STATUS_SPRITE0_HIT = 0x40;
constexpr std::uint8_t STATUS_SPRITE_OVERFLOW = 0x20;
constexpr std::uint8_t MASK_GRAYSCALE = 0x01;

/** 预渲染行的第 1 点：vblank、精灵 0 命中与精灵溢出清除；第 339 点：奇数帧在这里决定是否跳过最后一点，此后下一帧 vblank 的位置才确定。 */
constexpr int VBLANK_END_DOT = 1;
constexpr int SKIP_DECISION_DOT = 339;

/** 上电时调色板的内容（实测值），真机每次上电不同。 */
constexpr std::array<std::uint8_t, 32> POWER_UP_PALETTE = {0x09, 0x01, 0x00, 0x01, 0x00, 0x02, 0x02, 0x0D, 0x08, 0x10, 0x08, 0x24, 0x00, 0x00, 0x04, 0x2C,
                                                           0x09, 0x01, 0x34, 0x03, 0x00, 0x04, 0x00, 0x14, 0x08, 0x3A, 0x00, 0x02, 0x00, 0x20, 0x2C, 0x08};

}  // namespace

Ppu2c02::Ppu2c02(hw::Scheduler& scheduler, hw::PpuBus& bus, hw::IrqLine& nmi, hw::ClockDomain domain, hw::MasterTime phase) : Ppu2c02(scheduler, bus, nmi, domain, phase, Options{SCANLINES_PER_FRAME, true}) {}

Ppu2c02::Ppu2c02(hw::Scheduler& scheduler, hw::PpuBus& bus, hw::IrqLine& nmi, hw::ClockDomain domain, hw::MasterTime phase, Options options)
    : scheduler_(scheduler),
      bus_(bus),
      nmi_(nmi),
      nmiSource_(nmi.addSource()),
      domain_(domain),
      phase_(phase),
      options_(options),
      preRenderLine_(options.scanlinesPerFrame - 1),
      vblankStartIndex_(options.vblankLine * DOTS_PER_SCANLINE + 1),
      rgbaTable_(buildPpuPalette()) {
  if (domain_.ticksPerCycle < 1) throw shared::CoreError(hw::ERROR_INVALID_ARGUMENT, "PPU 点时钟的主时钟周期数必须大于 0");
  if (phase_ < 0) throw shared::CoreError(hw::ERROR_INVALID_ARGUMENT, "PPU 的相位不能为负");
  if (options_.vblankLine < 241) throw shared::CoreError(hw::ERROR_INVALID_ARGUMENT, "PPU 的 vblank 起始行不能小于 241");
  if (options_.scanlinesPerFrame <= options_.vblankLine) throw shared::CoreError(hw::ERROR_INVALID_ARGUMENT, "PPU 每帧的行数必须大于 vblank 起始行");
  for (auto& buffer : frameBuffers_) buffer.assign(static_cast<std::size_t>(FRAME_WIDTH) * FRAME_HEIGHT, 0xFF000000U);
}

// ---------------------------------------------------------------- 复位与时间

void Ppu2c02::reset(bool power) {
  if (eventId_ != hw::NO_EVENT) scheduler_.cancel(eventId_);
  eventId_ = hw::NO_EVENT;

  ctrl_ = 0;
  mask_ = 0;
  w_ = false;
  readBuffer_ = 0;
  openBus_ = 0;
  suppressVbl_ = false;
  oddFrame_ = false;
  scanline_ = 0;
  dot_ = 0;
  spriteCount_ = 0;
  sprite0InLine_ = false;
  nextSpriteCount_ = 0;
  nextSprite0_ = false;
  shiftPatternLow_ = shiftPatternHigh_ = shiftAttributeLow_ = shiftAttributeHigh_ = 0;
  nextTileId_ = nextAttribute_ = nextPatternLow_ = nextPatternHigh_ = 0;
  if (power) {
    vblank_ = false;
    sprite0Hit_ = false;
    spriteOverflow_ = false;
    oamAddr_ = 0;
    v_ = 0;
    t_ = 0;
    fineX_ = 0;
    // 真机上 OAM 与调色板的上电内容随机；OAM 取 $FF 让精灵都在屏幕外。
    oam_.fill(0xFF);
    secondary_.fill(0xFF);
    palette_ = POWER_UP_PALETTE;
    frameCount_ = 0;
    for (auto& buffer : frameBuffers_) std::fill(buffer.begin(), buffer.end(), 0xFF000000U);
    drawIndex_ = 0;
  }
  nextDotTime_ = scheduler_.now() + phase_;
  updateNmi();
  scheduleNextEvent();
}

void Ppu2c02::catchUp(hw::MasterTime now) {
  while (nextDotTime_ <= now) runDot();
}

std::array<std::uint32_t, 64> Ppu2c02::colorTable() const {
  const std::size_t emphasis = static_cast<std::size_t>(mask_ >> 5) & 7;
  const unsigned colorMask = (mask_ & MASK_GRAYSCALE) != 0 ? 0x30U : 0x3FU;
  std::array<std::uint32_t, 64> table{};
  for (std::size_t color = 0; color < table.size(); ++color) table[color] = rgbaTable_[emphasis * 64 + (color & colorMask)];
  return table;
}

hw::VideoPosition Ppu2c02::positionAt(hw::MasterTime now) const {
  // frameCount_ 在第 240 行第 0 点执行时加 1：下一个点已经越过它时，要减回来才是帧序号（每帧从第 0 行起算）。
  const bool pastPublish = scanline_ > 240 || (scanline_ == 240 && dot_ > 0);
  std::int64_t frame = static_cast<std::int64_t>(frameCount_) - (pastPublish ? 1 : 0);
  int line = scanline_;
  int dot = dot_;
  bool odd = oddFrame_;
  // 开始时间不晚于 now 的点都算已经执行。
  std::int64_t remaining = now >= nextDotTime_ ? static_cast<std::int64_t>((now - nextDotTime_) / domain_.ticksPerCycle) + 1 : 0;
  // 按行推进；奇数帧且背景开启时，预渲染行少最后一个点（背景开关按现在的值算，之后的改写无从预知）。
  const bool backgroundOn = (mask_ & 0x08) != 0;
  while (remaining > 0) {
    const bool shortLine = options_.oddFrameSkip && line == preRenderLine_ && odd && backgroundOn;
    const int left = std::max(1, (shortLine ? DOTS_PER_SCANLINE - 1 : DOTS_PER_SCANLINE) - dot);
    if (remaining < left) {
      dot += static_cast<int>(remaining);
      break;
    }
    remaining -= left;
    dot = 0;
    if (++line == options_.scanlinesPerFrame) {
      line = 0;
      odd = !odd;
      ++frame;
    }
  }
  return hw::VideoPosition{static_cast<std::uint64_t>(frame), line, dot};
}

void Ppu2c02::halt() {
  if (eventId_ != hw::NO_EVENT) scheduler_.cancel(eventId_);
  eventId_ = hw::NO_EVENT;
  nmi_.set(nmiSource_, false);
}

void Ppu2c02::onEvent(hw::MasterTime when, std::uint32_t /*tag*/) {
  eventId_ = hw::NO_EVENT;
  catchUp(when);
  scheduleNextEvent();
}

void Ppu2c02::scheduleNextEvent() {
  if (eventId_ != hw::NO_EVENT) scheduler_.cancel(eventId_);
  const int current = scanline_ * DOTS_PER_SCANLINE + dot_;
  const int vblankEnd = preRenderLine_ * DOTS_PER_SCANLINE + VBLANK_END_DOT;
  const int skipDecision = preRenderLine_ * DOTS_PER_SCANLINE + SKIP_DECISION_DOT;
  const int dotsPerFrame = DOTS_PER_SCANLINE * options_.scanlinesPerFrame;
  int target = 0;
  // 下一个会改变 NMI 线（或决定下一帧长度）的点；都已过去时取下一帧的 vblank 起点。
  if (current <= vblankStartIndex_) {
    target = vblankStartIndex_ - current;
  } else if (current <= vblankEnd) {
    target = vblankEnd - current;
  } else if (current <= skipDecision) {
    target = skipDecision - current;
  } else {
    target = vblankStartIndex_ + dotsPerFrame - current;
  }
  eventId_ = scheduler_.scheduleAt(nextDotTime_ + static_cast<hw::MasterTime>(target) * domain_.ticksPerCycle, *this, 0);
}

void Ppu2c02::updateNmi() { nmi_.set(nmiSource_, vblank_ && (ctrl_ & CTRL_NMI_ENABLE) != 0); }

void Ppu2c02::runDot() {
  const int line = scanline_;
  const int dot = dot_;
  // 背景开关写入后要过 1 个点才对跳点的判断生效。
  const bool backgroundEnabledBefore = backgroundWasEnabled_;
  const bool renderLine = line < 240 || line == preRenderLine_;
  const bool rendering = renderingEnabled();

  if (renderLine) {
    if (rendering) {
      if ((dot >= 2 && dot <= 257) || (dot >= 321 && dot <= 337)) shiftBackground();
      fetchBackground(dot);
      if (line < 240 && dot >= 1 && dot <= 256) evaluateSprites(line, dot);
      if (dot >= 257 && dot <= 320) fetchSprites(line, dot);
    }
    if (line < 240 && dot >= 1 && dot <= 256) drawPixel(line, dot - 1);
  }

  if (line == options_.vblankLine && dot == 1) {
    if (!suppressVbl_) vblank_ = true;
    suppressVbl_ = false;
    updateNmi();
  } else if (line == preRenderLine_ && dot == VBLANK_END_DOT) {
    vblank_ = false;
    sprite0Hit_ = false;
    spriteOverflow_ = false;
    updateNmi();
  } else if (line == 240 && dot == 0) {
    publishFrame();
  }

  // 前进一个点；奇数帧且背景开启时，预渲染行跳过最后一点。
  nextDotTime_ += domain_.ticksPerCycle;
  const bool skip = options_.oddFrameSkip && line == preRenderLine_ && dot == SKIP_DECISION_DOT && oddFrame_ && backgroundEnabledBefore;
  backgroundWasEnabled_ = (mask_ & 0x08) != 0;
  if (skip || ++dot_ == DOTS_PER_SCANLINE) {
    dot_ = 0;
    if (++scanline_ == options_.scanlinesPerFrame) {
      scanline_ = 0;
      oddFrame_ = !oddFrame_;
    }
  }
}

void Ppu2c02::publishFrame() {
  drawIndex_ ^= 1;
  ++frameCount_;
}

hw::FrameBuffer Ppu2c02::frame() const {
  if (frameCount_ == 0) return hw::FrameBuffer{};
  // 最近完成的一帧在不用于绘制的那块缓冲里。
  const auto& shown = frameBuffers_[static_cast<std::size_t>(drawIndex_ ^ 1)];
  return hw::FrameBuffer{FRAME_WIDTH, FRAME_HEIGHT, std::span<const std::uint32_t>(shown)};
}

// ---------------------------------------------------------------- 寄存器

hw::BusRead Ppu2c02::read(std::uint16_t address) {
  catchUp(scheduler_.now());
  std::uint8_t value = openBus_;
  switch (address & 7) {
    case 2:
      value = readStatus();
      break;
    case 4:
      value = readOamData();
      break;
    case 7:
      value = readData();
      break;
    default:
      break;
  }
  openBus_ = value;
  return hw::driveBus(value);
}

void Ppu2c02::write(std::uint16_t address, std::uint8_t value) {
  catchUp(scheduler_.now());
  openBus_ = value;
  switch (address & 7) {
    case 0:
      ctrl_ = value;
      t_ = static_cast<std::uint16_t>((t_ & 0xF3FF) | ((value & 3) << 10));
      updateNmi();
      break;
    case 1:
      mask_ = value;
      break;
    case 3:
      oamAddr_ = value;
      break;
    case 4:
      writeOamData(value);
      break;
    case 5:
      writeScroll(value);
      break;
    case 6:
      writeAddress(value);
      break;
    case 7:
      writeData(value);
      break;
    default:
      break;
  }
}

std::uint8_t Ppu2c02::readStatus() {
  std::uint8_t value = static_cast<std::uint8_t>(openBus_ & 0x1F);
  if (vblank_) value |= STATUS_VBLANK;
  if (sprite0Hit_) value |= STATUS_SPRITE0_HIT;
  if (spriteOverflow_) value |= STATUS_SPRITE_OVERFLOW;
  // 在 vblank 置位前一个点读取：读到 0，且本帧不置位、不产生 NMI。
  if (scanline_ == options_.vblankLine && dot_ == 1) suppressVbl_ = true;
  vblank_ = false;
  w_ = false;
  updateNmi();
  return value;
}

std::uint8_t Ppu2c02::readOamData() {
  const bool evaluating = renderingEnabled() && scanline_ < 240;
  if (evaluating) {
    if (dot_ >= 1 && dot_ <= 64) return 0xFF;
    if (dot_ >= 65 && dot_ <= 256) return oamBuffer_;
    if (dot_ >= 257 && dot_ <= 320) return secondary_[static_cast<std::size_t>(((dot_ - 257) >> 3) * 4 + std::min((dot_ - 257) & 7, 3))];
    return secondary_[0];
  }
  std::uint8_t value = oam_[oamAddr_];
  // 精灵属性字节的位 2-4 不存在，读出为 0。
  if ((oamAddr_ & 3) == 2) value &= 0xE3;
  return value;
}

void Ppu2c02::writeOamData(std::uint8_t value) {
  if (renderingEnabled() && (scanline_ < 240 || scanline_ == preRenderLine_)) {
    // 渲染期间的写入不改变 OAM，只让地址的高 6 位加 1。
    oamAddr_ = static_cast<std::uint8_t>(oamAddr_ + 4);
    return;
  }
  oam_[oamAddr_++] = value;
}

void Ppu2c02::writeScroll(std::uint8_t value) {
  if (!w_) {
    t_ = static_cast<std::uint16_t>((t_ & 0xFFE0) | (value >> 3));
    fineX_ = value & 7;
  } else {
    t_ = static_cast<std::uint16_t>((t_ & 0x0C1F) | ((value & 7) << 12) | ((value & 0xF8) << 2));
  }
  w_ = !w_;
}

void Ppu2c02::writeAddress(std::uint8_t value) {
  if (!w_) {
    t_ = static_cast<std::uint16_t>((t_ & 0x00FF) | ((value & 0x3F) << 8));
  } else {
    t_ = static_cast<std::uint16_t>((t_ & 0xFF00) | value);
    v_ = t_;
  }
  w_ = !w_;
}

std::uint8_t Ppu2c02::readData() {
  const std::uint16_t address = v_ & 0x3FFF;
  std::uint8_t value = 0;
  if (address >= 0x3F00) {
    // 调色板直接读出（不经缓冲），高两位来自数据总线上的旧值；缓冲装入的是调色板下面的名称表。
    value = palette_[paletteIndex(address)] & 0x3F;
    if ((mask_ & MASK_GRAYSCALE) != 0) value &= 0x30;
    value = static_cast<std::uint8_t>(value | (openBus_ & 0xC0));
    readBuffer_ = readVram(static_cast<std::uint16_t>(address & 0x2FFF));
  } else {
    value = readBuffer_;
    readBuffer_ = readVram(address);
  }
  incrementVramAddress();
  return value;
}

void Ppu2c02::writeData(std::uint8_t value) {
  const std::uint16_t address = v_ & 0x3FFF;
  if (address >= 0x3F00) {
    palette_[paletteIndex(address)] = value & 0x3F;
  } else if (!(options_.dropVramWriteWhileRendering && renderingEnabled() && (scanline_ < 240 || scanline_ == preRenderLine_))) {
    bus_.write(address, value);
  }
  incrementVramAddress();
}

void Ppu2c02::incrementVramAddress() {
  if (renderingEnabled() && (scanline_ < 240 || scanline_ == preRenderLine_)) {
    // 渲染期间访问 $2007：同时递增粗 X 与 Y，而不是加 1 或 32。
    incrementCoarseX();
    incrementY();
  } else {
    v_ = static_cast<std::uint16_t>((v_ + ((ctrl_ & CTRL_INCREMENT_32) != 0 ? 32 : 1)) & 0x7FFF);
  }
}

std::uint8_t Ppu2c02::paletteIndex(std::uint16_t address) const {
  std::uint8_t index = address & 0x1F;
  // $3F10、$3F14、$3F18、$3F1C 是 $3F00、$3F04、$3F08、$3F0C 的镜像。
  if ((index & 0x13) == 0x10) index &= 0x0F;
  return index;
}

std::uint8_t Ppu2c02::readVram(std::uint16_t address) { return bus_.read(static_cast<std::uint16_t>(address & 0x3FFF)); }

}  // namespace ember::logic::hardware
