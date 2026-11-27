/*
 * ------------------------------------------------------------------------
 * 名称：ppu-um6576.cpp
 * 说明：6576 PPU 的实现：寄存器、逐行渲染、行间的调度。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-15
 * 备注：NES 通用调色板是常见的 2C02 实测表。
 * ------------------------------------------------------------------------
 */

#include "logic/hardware/ppu-um6576/ppu-um6576.hpp"

#include <algorithm>

#include "contracts/hardware/hardware-error.hpp"
#include "shared/errors/core-error.hpp"

namespace ember::logic::hardware {

namespace {

namespace hw = contracts::hardware;

/** 通用 NES 调色板（每项 R、G、B）。 */
constexpr std::uint8_t NES_PALETTE[64][3] = {
    {84, 84, 84},    {0, 30, 116},    {8, 16, 144},    {48, 0, 136},    {68, 0, 100},    {92, 0, 48},     {84, 4, 0},      {60, 24, 0},
    {32, 42, 0},     {8, 58, 0},      {0, 64, 0},      {0, 60, 0},      {0, 50, 60},     {0, 0, 0},       {0, 0, 0},       {0, 0, 0},
    {152, 150, 152}, {8, 76, 196},    {48, 50, 236},   {92, 30, 228},   {136, 20, 176},  {160, 20, 100},  {152, 34, 32},   {120, 60, 0},
    {84, 90, 0},     {40, 114, 0},    {8, 124, 0},     {0, 118, 40},    {0, 102, 120},   {0, 0, 0},       {0, 0, 0},       {0, 0, 0},
    {236, 238, 236}, {76, 154, 236},  {120, 124, 236}, {176, 98, 236},  {228, 84, 236},  {236, 88, 180},  {236, 106, 100}, {212, 136, 32},
    {160, 170, 0},   {116, 196, 0},   {76, 208, 32},   {56, 204, 108},  {56, 180, 204},  {60, 60, 60},    {0, 0, 0},       {0, 0, 0},
    {236, 238, 236}, {168, 204, 236}, {188, 188, 236}, {212, 178, 236}, {236, 174, 236}, {236, 174, 212}, {236, 180, 176}, {228, 196, 144},
    {204, 210, 120}, {180, 222, 120}, {168, 226, 144}, {152, 226, 180}, {160, 214, 228}, {160, 162, 160}, {0, 0, 0},       {0, 0, 0}};

constexpr int LINE_PAD = 8;
constexpr std::uint32_t OPAQUE_BLACK = 0xFF000000U;

/** $2000、$2001 的位。 */
constexpr std::uint8_t CTRL_NMI = 0x80;
constexpr std::uint8_t CTRL_SPRITE_16 = 0x20;
constexpr std::uint8_t CTRL_INCREMENT_32 = 0x04;
constexpr std::uint8_t MASK_MONO = 0x01;
constexpr std::uint8_t MASK_BG_LEFT = 0x02;
constexpr std::uint8_t MASK_SP_LEFT = 0x04;
constexpr std::uint8_t MASK_BG = 0x08;
constexpr std::uint8_t MASK_SP = 0x10;

/** 6 位色号按 $2001 的黑白与增强位换算成显示的颜色（内存顺序 R、G、B、A）。 */
std::uint32_t displayColor(unsigned entry, std::uint8_t mask) {
  // 真机实测：$2001 位 5 增强绿、位 6 增强红、位 7 增强蓝（PAL 的顺序），位 0 为黑白。
  const bool emphasizeGreen = (mask & 0x20) != 0;
  const bool emphasizeRed = (mask & 0x40) != 0;
  const bool emphasizeBlue = (mask & 0x80) != 0;
  const bool emphasis = emphasizeGreen || emphasizeRed || emphasizeBlue;
  entry &= 0x3FU;
  if ((mask & MASK_MONO) != 0) entry &= 0x30U;
  unsigned red = NES_PALETTE[entry][0];
  unsigned green = NES_PALETTE[entry][1];
  unsigned blue = NES_PALETTE[entry][2];
  if (emphasis) {
    if (!emphasizeRed) red = red * 3 / 4;
    if (!emphasizeGreen) green = green * 3 / 4;
    if (!emphasizeBlue) blue = blue * 3 / 4;
  }
  return OPAQUE_BLACK | (blue << 16) | (green << 8) | red;
}

}  // namespace

PpuUm6576::PpuUm6576(hw::Scheduler& scheduler, hw::PpuBus& bus, hw::IrqLine& nmi, hw::ClockDomain domain, hw::MasterTime phase)
    : scheduler_(scheduler), bus_(bus), nmi_(nmi), nmiSource_(nmi.addSource()), domain_(domain), phase_(phase) {
  if (domain_.ticksPerCycle < 1) throw shared::CoreError(hw::ERROR_INVALID_ARGUMENT, "PPU 点的主时钟周期数必须大于 0");
  if (phase_ < 0) throw shared::CoreError(hw::ERROR_INVALID_ARGUMENT, "PPU 的相位不能为负");
  drawing_.assign(FRAME_WIDTH * FRAME_HEIGHT, OPAQUE_BLACK);
  published_ = drawing_;
}

hw::FrameBuffer PpuUm6576::frame() const {
  if (frameCount_ == 0) return hw::FrameBuffer{};
  return hw::FrameBuffer{FRAME_WIDTH, FRAME_HEIGHT, std::span<const std::uint32_t>(published_)};
}

void PpuUm6576::reset(bool power) {
  if (eventId_ != hw::NO_EVENT) scheduler_.cancel(eventId_);
  eventId_ = hw::NO_EVENT;
  ctrl_ = 0;
  mask_ = 0;
  status_ = 0;
  oamAddress_ = 0;
  reg2008_ = 0;
  scrollX_ = 0;
  scrollY_ = 0;
  scrollToggle_ = false;
  addressToggle_ = false;
  address_ = 0;
  readBuffer_ = 0;
  lastWrite_ = 0;
  if (power) {
    oam_.fill(0);
    palette_.fill(0);
    frameCount_ = 0;
  }
  std::fill(drawing_.begin(), drawing_.end(), OPAQUE_BLACK);
  published_ = drawing_;
  nmi_.set(nmiSource_, false);
  line_ = 0;
  nextLineTime_ = scheduler_.now() + phase_;
  scheduleNextLine();
}

void PpuUm6576::scheduleNextLine() { eventId_ = scheduler_.scheduleAt(nextLineTime_, *this, 0); }

void PpuUm6576::updateNmi() { nmi_.set(nmiSource_, (status_ & 0x80) != 0 && (ctrl_ & CTRL_NMI) != 0); }

void PpuUm6576::onEvent(hw::MasterTime /*when*/, std::uint32_t /*tag*/) {
  eventId_ = hw::NO_EVENT;
  processLine();
  nextLineTime_ += static_cast<hw::MasterTime>(DOTS_PER_SCANLINE) * domain_.ticksPerCycle;
  scheduleNextLine();
}

void PpuUm6576::processLine() {
  if (line_ < static_cast<int>(FRAME_HEIGHT)) {
    renderLine(line_);
  } else if (line_ == static_cast<int>(FRAME_HEIGHT)) {
    published_ = drawing_;
    ++frameCount_;
  } else if (line_ == VBLANK_LINE) {
    status_ = static_cast<std::uint8_t>(status_ | 0x80);
    updateNmi();
  } else if (line_ == PRE_RENDER_LINE) {
    status_ &= 0x1F;
    updateNmi();
  }
  line_ = (line_ + 1) % SCANLINES_PER_FRAME;
}

hw::BusRead PpuUm6576::peek(std::uint16_t address) {
  switch (address) {
    case 0x2002:
      return hw::driveBus(static_cast<std::uint8_t>((status_ & 0xE0) | (lastWrite_ & 0x1F)));
    case 0x2004: {
      std::uint8_t value = oam_[oamAddress_];
      if ((oamAddress_ & 3) == 2) value |= 0x1C;
      return hw::driveBus(value);
    }
    case 0x2007:
      return hw::driveBus(readBuffer_);
    case 0x2008:
      return hw::driveBus(reg2008_);
    case 0x2000:
    case 0x2001:
    case 0x2003:
    case 0x2005:
    case 0x2006:
    case 0x2009:
      return hw::driveBus(0);
    case 0x3FFF:
      // 真机实测读到 $68，原因不明
      return hw::driveBus(0x68);
    default:
      if (address >= 0x2040 && address < 0x2080) return hw::driveBus(static_cast<std::uint8_t>(palette_[address & 0x3F] | 0x40));
      return hw::releaseBus();
  }
}

hw::BusRead PpuUm6576::read(std::uint16_t address) {
  const hw::BusRead result = peek(address);
  switch (address) {
    case 0x2002:
      status_ &= 0x7F;
      scrollToggle_ = false;
      addressToggle_ = false;
      updateNmi();
      break;
    case 0x2004:
      ++oamAddress_;
      break;
    case 0x2007:
      readBuffer_ = bus_.read(address_);
      address_ = static_cast<std::uint16_t>(address_ + ((ctrl_ & CTRL_INCREMENT_32) != 0 ? 32 : 1));
      break;
    default:
      break;
  }
  return result;
}

void PpuUm6576::write(std::uint16_t address, std::uint8_t value) {
  if (address <= 0x2007) lastWrite_ = value;
  switch (address) {
    case 0x2000:
      ctrl_ = value;
      updateNmi();
      break;
    case 0x2001:
      mask_ = value;
      break;
    case 0x2003:
      oamAddress_ = value;
      break;
    case 0x2004:
      oam_[oamAddress_++] = value;
      break;
    case 0x2005:
      if (scrollToggle_) {
        scrollY_ = value;
      } else {
        scrollX_ = value;
      }
      scrollToggle_ = !scrollToggle_;
      break;
    case 0x2006:
      if (addressToggle_) {
        address_ = static_cast<std::uint16_t>((address_ & 0xFF00) | value);
      } else {
        address_ = static_cast<std::uint16_t>((address_ & 0x00FF) | (value << 8));
      }
      addressToggle_ = !addressToggle_;
      break;
    case 0x2007:
      bus_.write(address_, value);
      address_ = static_cast<std::uint16_t>(address_ + ((ctrl_ & CTRL_INCREMENT_32) != 0 ? 32 : 1));
      break;
    case 0x2008:
      reg2008_ = value;
      break;
    default:
      if (address >= 0x2040 && address < 0x2080) palette_[address & 0x3F] = value & 0x3F;
      break;
  }
}

void PpuUm6576::renderLine(int line) {
  backgroundOpaque_.fill(0);
  if ((mask_ & MASK_BG) != 0) {
    int tileY = (line + scrollY_) / 8;
    int tileBase;
    // 真机实测：$2008 位 0 为 1 时名称表在显存 $2000 起，只有一页（不随 $2000 位 1-0 变化）。
    if ((reg2008_ & 1) != 0) {
      tileBase = 0x2000;
      if (tileY >= 32) tileY -= 32;
    } else {
      tileBase = (ctrl_ & 3) * 2048;
      if (tileY >= 32) {
        tileY -= 32;
        tileBase += (ctrl_ & 2) != 0 ? -4096 : 4096;
      }
    }
    int destination = LINE_PAD;
    const bool sixteenColors = (reg2008_ & 0x80) != 0;
    for (int index = 0; index < 33; ++index) {
      const int tileX = scrollX_ / 8 + index;
      int tileAddress;
      if ((reg2008_ & 1) != 0 || (tileX & 0x20) == 0) {
        tileAddress = tileBase + tileY * 32 * 2 + (tileX & 31) * 2;
      } else {
        tileAddress = tileBase + 2048 + tileY * 32 * 2 + (tileX & 31) * 2;
      }
      const int tileName = bus_.read(static_cast<std::uint16_t>(tileAddress + 1)) * 256 + bus_.read(static_cast<std::uint16_t>(tileAddress));
      int mask = index != 0 ? 0x80 : (0x80 >> (scrollX_ & 7));
      int tileWidth;
      if (index == 0) {
        tileWidth = 8 - (scrollX_ & 7);
      } else if (index == 32) {
        tileWidth = scrollX_ & 7;
      } else {
        tileWidth = 8;
      }
      const int y = (line + scrollY_) & 7;
      if (sixteenColors) {
        const int bank = (tileName >> 12) & 0x0C;
        const int offset = (tileName & 0xFFE) * 16;
        const std::uint8_t plane0 = bus_.read(static_cast<std::uint16_t>(offset + 0 + y));
        const std::uint8_t plane1 = bus_.read(static_cast<std::uint16_t>(offset + 8 + y));
        const std::uint8_t plane2 = bus_.read(static_cast<std::uint16_t>(offset + 16 + y));
        const std::uint8_t plane3 = bus_.read(static_cast<std::uint16_t>(offset + 24 + y));
        for (int x = 0; x < tileWidth; ++x) {
          int color = 0;
          if ((plane0 & mask) != 0) color |= 1;
          if ((plane1 & mask) != 0) color |= 2;
          if ((plane2 & mask) != 0) color |= 4;
          if ((plane3 & mask) != 0) color |= 8;
          backgroundOpaque_[destination] = color != 0;
          // 真机实测：颜色号为 0 的像素透明，显示调色板第 0 项，不用调色板组自己的第 0 项。
          lineBuffer_[destination++] = color != 0 ? palette_[bank * 4 + color] : palette_[0];
          mask >>= 1;
        }
      } else {
        const int bank = tileName >> 12;
        const int offset = (tileName & 0xFFF) * 16;
        const std::uint8_t plane0 = bus_.read(static_cast<std::uint16_t>(offset + 0 + y));
        const std::uint8_t plane1 = bus_.read(static_cast<std::uint16_t>(offset + 8 + y));
        for (int x = 0; x < tileWidth; ++x) {
          int color = 0;
          if ((plane0 & mask) != 0) color |= 1;
          if ((plane1 & mask) != 0) color |= 2;
          backgroundOpaque_[destination] = color != 0;
          lineBuffer_[destination++] = color != 0 ? palette_[bank * 4 + color] : palette_[0];
          mask >>= 1;
        }
      }
    }
    // 真机实测：$2001 位 1 为 0 时背景最左 8 个像素不显示。
    if ((mask_ & MASK_BG_LEFT) == 0) {
      for (int x = 8; x < 16; ++x) {
        lineBuffer_[x] = palette_[0];
        backgroundOpaque_[x] = 0;
      }
    }
  } else {
    lineBuffer_.fill(0);
  }

  if ((mask_ & MASK_SP) != 0) {
    spriteOccupied_.fill(0);
    const int height = (ctrl_ & CTRL_SPRITE_16) != 0 ? 16 : 8;
    int inLine = 0;
    for (int sprite = 0; sprite < 64; ++sprite) {
      const std::uint8_t* entry = &oam_[static_cast<std::size_t>(sprite) * 4];
      const int top = entry[0] + 1;
      if (line < top || line >= top + height) continue;
      // 真机实测：同一行超过 8 个精灵时 $2002 位 5 置 1（仍然全部绘制）。
      if (++inLine > 8) status_ |= 0x20;
      const int left = entry[3];
      int tileName = entry[1];
      const std::uint8_t attribute = entry[2];
      int y = line - top;
      if ((attribute & 0x80) != 0) y = height - 1 - y;
      const int tileBase = ((reg2008_ >> 2) & 3) << 12;
      if (height == 16) {
        tileName &= 0xFE;
        if (y >= 8) {
          ++tileName;
          y -= 8;
        }
      }
      const int offset = tileBase + tileName * 16;
      const std::uint8_t plane0 = bus_.read(static_cast<std::uint16_t>(offset + 0 + y));
      const std::uint8_t plane1 = bus_.read(static_cast<std::uint16_t>(offset + 8 + y));
      for (int x = 0; x < 8; ++x) {
        const int destination = left + LINE_PAD + x;
        const int mask = (attribute & 0x40) != 0 ? (0x01 << x) : (0x80 >> x);
        int color = 0;
        if ((plane0 & mask) != 0) color |= 1;
        if ((plane1 & mask) != 0) color |= 2;
        if (color == 0) continue;
        // 真机实测：$2001 位 2 为 0 时精灵最左 8 个像素不显示。
        if (left + x < 8 && (mask_ & MASK_SP_LEFT) == 0) continue;
        if (spriteOccupied_[destination] != 0) continue;
        spriteOccupied_[destination] = 1;
        // 精灵 0 与不透明的背景像素重合：$2002 位 6（最右一列不算）。
        if (sprite == 0 && backgroundOpaque_[destination] != 0 && left + x != 255) status_ |= 0x40;
        // 在背景之后的精灵：背景不透明处被盖住（仍然挡住序号更大的精灵）。
        if ((attribute & 0x20) != 0 && backgroundOpaque_[destination] != 0) continue;
        lineBuffer_[destination] = palette_[16 + (attribute & 3) * 4 + color];
      }
    }
  }
  storeLine(line);
}

void PpuUm6576::storeLine(int line) {
  std::uint32_t* row = &drawing_[static_cast<std::size_t>(line) * FRAME_WIDTH];
  for (std::uint32_t x = 0; x < FRAME_WIDTH; ++x) row[x] = displayColor(lineBuffer_[x + LINE_PAD], mask_);
}

std::array<std::uint32_t, 64> PpuUm6576::colorTable() const {
  std::array<std::uint32_t, 64> table{};
  for (unsigned color = 0; color < table.size(); ++color) table[color] = displayColor(color, mask_);
  return table;
}

}  // namespace ember::logic::hardware
