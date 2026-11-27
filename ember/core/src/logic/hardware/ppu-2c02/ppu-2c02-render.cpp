/*
 * ------------------------------------------------------------------------
 * 名称：ppu-2c02-render.cpp
 * 说明：2C02 的逐点渲染：背景图案与属性的取数和移位、滚动寄存器的递增与复制、精灵评估与取数、像素合成与精灵 0 命中。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * 备注：背景每 8 个点取一个图块（名称表、属性表、图案低位、图案高位），第 256 点递增 Y，第 257 点复制横向位，预渲染行第 280-304 点复制纵向位；
 *       精灵评估在可见行的第 1-64 点清次级 OAM、第 65-256 点评估（奇数点读、偶数点写），第 257-320 点取精灵图案；
 *       预渲染行不评估精灵，所以第 0 行没有精灵。
 * ------------------------------------------------------------------------
 */

#include <algorithm>

#include "logic/hardware/ppu-2c02/ppu-2c02.hpp"

namespace ember::logic::hardware {

namespace {

constexpr std::uint8_t CTRL_SPRITE_TABLE = 0x08;
constexpr std::uint8_t CTRL_BACKGROUND_TABLE = 0x10;
constexpr std::uint8_t CTRL_SPRITE_16 = 0x20;
constexpr std::uint8_t MASK_GRAYSCALE = 0x01;
constexpr std::uint8_t MASK_BACKGROUND_LEFT = 0x02;
constexpr std::uint8_t MASK_SPRITES_LEFT = 0x04;
constexpr std::uint8_t MASK_BACKGROUND = 0x08;
constexpr std::uint8_t MASK_SPRITES = 0x10;

constexpr std::uint8_t ATTRIBUTE_BEHIND_BACKGROUND = 0x20;
constexpr std::uint8_t ATTRIBUTE_FLIP_HORIZONTAL = 0x40;
constexpr std::uint8_t ATTRIBUTE_FLIP_VERTICAL = 0x80;

/** 一个字节内的位顺序反转（水平翻转精灵图案用）。 */
std::uint8_t reverseBits(std::uint8_t value) {
  value = static_cast<std::uint8_t>(((value & 0xF0) >> 4) | ((value & 0x0F) << 4));
  value = static_cast<std::uint8_t>(((value & 0xCC) >> 2) | ((value & 0x33) << 2));
  value = static_cast<std::uint8_t>(((value & 0xAA) >> 1) | ((value & 0x55) << 1));
  return value;
}

}  // namespace

// ---------------------------------------------------------------- 背景

void Ppu2c02::shiftBackground() {
  shiftPatternLow_ = static_cast<std::uint16_t>(shiftPatternLow_ << 1);
  shiftPatternHigh_ = static_cast<std::uint16_t>(shiftPatternHigh_ << 1);
  shiftAttributeLow_ = static_cast<std::uint16_t>(shiftAttributeLow_ << 1);
  shiftAttributeHigh_ = static_cast<std::uint16_t>(shiftAttributeHigh_ << 1);
}

void Ppu2c02::loadBackgroundShifters() {
  shiftPatternLow_ = static_cast<std::uint16_t>((shiftPatternLow_ & 0xFF00) | nextPatternLow_);
  shiftPatternHigh_ = static_cast<std::uint16_t>((shiftPatternHigh_ & 0xFF00) | nextPatternHigh_);
  shiftAttributeLow_ = static_cast<std::uint16_t>((shiftAttributeLow_ & 0xFF00) | ((nextAttribute_ & 1) != 0 ? 0xFF : 0x00));
  shiftAttributeHigh_ = static_cast<std::uint16_t>((shiftAttributeHigh_ & 0xFF00) | ((nextAttribute_ & 2) != 0 ? 0xFF : 0x00));
}

void Ppu2c02::incrementCoarseX() {
  if ((v_ & 0x001F) == 31) {
    v_ = static_cast<std::uint16_t>((v_ & ~0x001F) ^ 0x0400);
  } else {
    ++v_;
  }
}

void Ppu2c02::incrementY() {
  if ((v_ & 0x7000) != 0x7000) {
    v_ = static_cast<std::uint16_t>(v_ + 0x1000);
    return;
  }
  v_ &= ~0x7000;
  int coarseY = (v_ & 0x03E0) >> 5;
  if (coarseY == 29) {
    coarseY = 0;
    v_ ^= 0x0800;
  } else if (coarseY == 31) {
    coarseY = 0;
  } else {
    ++coarseY;
  }
  v_ = static_cast<std::uint16_t>((v_ & ~0x03E0) | (coarseY << 5));
}

std::uint16_t Ppu2c02::backgroundTableBase() const {
  bool high = (ctrl_ & CTRL_BACKGROUND_TABLE) != 0;
  if (!high && options_.backgroundTableOverride) high = options_.backgroundTableOverride(static_cast<unsigned>((v_ >> 10) & 3U));
  return high ? 0x1000 : 0x0000;
}

void Ppu2c02::fetchBackground(int dot) {
  const bool fetching = (dot >= 1 && dot <= 257) || (dot >= 321 && dot <= 337);
  if (fetching) {
    switch ((dot - 1) & 7) {
      case 0:
        loadBackgroundShifters();
        nextTileId_ = readVram(static_cast<std::uint16_t>(0x2000 | (v_ & 0x0FFF)));
        break;
      case 2: {
        std::uint8_t attribute = readVram(static_cast<std::uint16_t>(0x23C0 | (v_ & 0x0C00) | ((v_ >> 4) & 0x38) | ((v_ >> 2) & 0x07)));
        // 属性字节里四个 2 位的调色板号，按粗 X、粗 Y 的第 1 位选择。
        if ((v_ & 0x0040) != 0) attribute >>= 4;
        if ((v_ & 0x0002) != 0) attribute >>= 2;
        nextAttribute_ = static_cast<std::uint8_t>(attribute & 3);
        break;
      }
      case 4:
        nextPatternLow_ = readVram(static_cast<std::uint16_t>(backgroundTableBase() + (nextTileId_ << 4) + ((v_ >> 12) & 7)));
        break;
      case 6:
        nextPatternHigh_ = readVram(static_cast<std::uint16_t>(backgroundTableBase() + (nextTileId_ << 4) + ((v_ >> 12) & 7) + 8));
        break;
      case 7:
        incrementCoarseX();
        break;
      default:
        break;
    }
  }
  if (dot == 256) incrementY();
  if (dot == 257) v_ = static_cast<std::uint16_t>((v_ & ~0x041F) | (t_ & 0x041F));
  if (scanline_ == preRenderLine_ && dot >= 280 && dot <= 304) v_ = static_cast<std::uint16_t>((v_ & ~0x7BE0) | (t_ & 0x7BE0));
}

// ---------------------------------------------------------------- 精灵

void Ppu2c02::evaluateSprites(int line, int dot) {
  // 第 1-64 点：次级 OAM 清为 $FF，此时读 $2004 也得到 $FF。
  if (dot <= 64) {
    oamBuffer_ = 0xFF;
    secondary_[static_cast<std::size_t>((dot - 1) >> 1)] = 0xFF;
    return;
  }
  if (dot == 65) {
    sprite0Added_ = false;
    spriteInRange_ = false;
    evaluationDone_ = false;
    secondaryAddress_ = 0;
    overflowCounter_ = 0;
    spriteN_ = (oamAddr_ >> 2) & 0x3F;
    spriteM_ = oamAddr_ & 3;
  } else if (dot == 256) {
    nextSprite0_ = sprite0Added_;
    nextSpriteCount_ = secondaryAddress_ >> 2;
  }

  // 奇数点读主 OAM。
  if ((dot & 1) != 0) {
    oamBuffer_ = oam_[oamAddr_];
    return;
  }

  // 偶数点写次级 OAM；次级 OAM 满了以后写变成读。
  const int height = (ctrl_ & CTRL_SPRITE_16) != 0 ? 16 : 8;
  const bool full = secondaryAddress_ >= 32;
  if (evaluationDone_) {
    spriteN_ = (spriteN_ + 1) & 0x3F;
    if (full) oamBuffer_ = secondary_[secondaryAddress_ & 31];
  } else {
    const int row = line - oamBuffer_;
    if (!spriteInRange_ && row >= 0 && row < height) spriteInRange_ = true;
    if (!full) {
      secondary_[secondaryAddress_] = oamBuffer_;
      if (spriteInRange_) {
        ++spriteM_;
        ++secondaryAddress_;
        if (spriteN_ == 0) sprite0Added_ = true;
        if ((secondaryAddress_ & 3) == 0) {
          // 一个精灵的 4 个字节复制完。
          spriteInRange_ = false;
          spriteM_ = 0;
          spriteN_ = (spriteN_ + 1) & 0x3F;
          if (spriteN_ == 0) evaluationDone_ = true;
        }
      } else {
        spriteN_ = (spriteN_ + 1) & 0x3F;
        if (spriteN_ == 0) evaluationDone_ = true;
      }
    } else {
      // 已找到 8 个精灵：继续找第 9 个来置溢出标志。硬件缺陷：不在范围内时 n 与 m 一起递增，
      // 于是把后面精灵的图块号、属性、X 当作 Y 坐标来比较。
      oamBuffer_ = secondary_[secondaryAddress_ & 31];
      if (spriteInRange_) {
        spriteOverflow_ = true;
        ++spriteM_;
        if (spriteM_ == 4) {
          spriteN_ = (spriteN_ + 1) & 0x3F;
          spriteM_ = 0;
        }
        if (overflowCounter_ == 0) {
          overflowCounter_ = 3;
        } else if (--overflowCounter_ == 0) {
          evaluationDone_ = true;
          spriteM_ = 0;
        }
      } else {
        spriteN_ = (spriteN_ + 1) & 0x3F;
        spriteM_ = (spriteM_ + 1) & 3;
        if (spriteN_ == 0) evaluationDone_ = true;
      }
    }
  }
  oamAddr_ = static_cast<std::uint8_t>((spriteN_ << 2) | (spriteM_ & 3));
}

void Ppu2c02::fetchSprites(int line, int dot) {
  // 第 257-320 点期间 OAM 地址保持为 0。
  oamAddr_ = 0;
  const int slot = (dot - 257) >> 3;
  const int phase = (dot - 257) & 7;
  if (phase == 0 && slot == 0) {
    // 预渲染行不评估精灵，所以第 0 行没有精灵。
spriteCount_ = line == preRenderLine_ ? 0 : nextSpriteCount_;
  sprite0InLine_ = line != preRenderLine_ && nextSprite0_;
  }
  if (phase != 4 && phase != 6) return;

  const bool used = slot < spriteCount_;
  const std::size_t base = static_cast<std::size_t>(slot) * 4;
  // 没用到的槽位取 Y=$FF、图块 $FF 的假图案，图案数据不用，但图案表照常被访问。
  const std::uint8_t y = used ? secondary_[base] : 0xFF;
  const std::uint8_t tile = used ? secondary_[base + 1] : 0xFF;
  const std::uint8_t attribute = used ? secondary_[base + 2] : 0xFF;
  const bool large = (ctrl_ & CTRL_SPRITE_16) != 0;
  const int height = large ? 16 : 8;
  int row = used ? line - y : 0;
  if ((attribute & ATTRIBUTE_FLIP_VERTICAL) != 0) row = height - 1 - row;

  std::uint16_t address = 0;
  if (!large) {
    address = static_cast<std::uint16_t>(((ctrl_ & CTRL_SPRITE_TABLE) != 0 ? 0x1000 : 0) + tile * 16 + (row & 7));
  } else {
    // 8x16 精灵：图块号的位 0 选图案表，位 1-7 选上下两个图块的第一个。
    address = static_cast<std::uint16_t>(((tile & 1) != 0 ? 0x1000 : 0) + (tile & 0xFE) * 16 + (row & 7) + ((row & 8) != 0 ? 16 : 0));
  }
  if (phase == 6) address = static_cast<std::uint16_t>(address + 8);
  std::uint8_t pattern = readVram(address);
  if (!used) pattern = 0;
  if ((attribute & ATTRIBUTE_FLIP_HORIZONTAL) != 0) pattern = reverseBits(pattern);

  SpriteUnit& unit = sprites_[static_cast<std::size_t>(slot)];
  if (phase == 4) {
    unit.patternLow = pattern;
    unit.attribute = attribute;
    unit.x = used ? secondary_[base + 3] : 0xFF;
  } else {
    unit.patternHigh = pattern;
  }
}

// ---------------------------------------------------------------- 像素

void Ppu2c02::drawPixel(int line, int x) {
  std::uint8_t backgroundPixel = 0;
  std::uint8_t backgroundPalette = 0;
  if ((mask_ & MASK_BACKGROUND) != 0 && (x >= 8 || (mask_ & MASK_BACKGROUND_LEFT) != 0)) {
    const std::uint16_t bit = static_cast<std::uint16_t>(0x8000U >> fineX_);
    backgroundPixel = static_cast<std::uint8_t>((((shiftPatternHigh_ & bit) != 0 ? 1 : 0) << 1) | ((shiftPatternLow_ & bit) != 0 ? 1 : 0));
    backgroundPalette = static_cast<std::uint8_t>((((shiftAttributeHigh_ & bit) != 0 ? 1 : 0) << 1) | ((shiftAttributeLow_ & bit) != 0 ? 1 : 0));
  }

  std::uint8_t spritePixel = 0;
  std::uint8_t spriteAttribute = 0;
  bool spriteIsZero = false;
  if ((mask_ & MASK_SPRITES) != 0 && (x >= 8 || (mask_ & MASK_SPRITES_LEFT) != 0)) {
    for (int i = 0; i < spriteCount_; ++i) {
      const SpriteUnit& unit = sprites_[static_cast<std::size_t>(i)];
      const int offset = x - unit.x;
      if (offset < 0 || offset >= 8) continue;
      const int shift = 7 - offset;
      const std::uint8_t pixel = static_cast<std::uint8_t>((((unit.patternHigh >> shift) & 1) << 1) | ((unit.patternLow >> shift) & 1));
      if (pixel == 0) continue;
      spritePixel = pixel;
      spriteAttribute = unit.attribute;
      spriteIsZero = i == 0 && sprite0InLine_;
      break;
    }
  }

  // 精灵 0 命中：两者的不透明像素重合（第 255 列除外）。
  if (spriteIsZero && backgroundPixel != 0 && x != 255) sprite0Hit_ = true;

  std::uint8_t entry = 0;
  if (backgroundPixel == 0 && spritePixel == 0) {
    // 背景与精灵都不显示时输出背景色；整个渲染关闭且 v 指向调色板时，输出 v 指向的那一项。
    entry = (!renderingEnabled() && (v_ & 0x3F00) == 0x3F00) ? palette_[paletteIndex(v_)] : palette_[0];
  } else if (spritePixel != 0 && (backgroundPixel == 0 || (spriteAttribute & ATTRIBUTE_BEHIND_BACKGROUND) == 0)) {
    if (options_.spritePaletteShifted && options_.spritePaletteShifted()) {
      const unsigned shifted = ((spriteAttribute & 3U) << 2) + 2U + spritePixel;
      entry = (shifted & 3U) == 0 ? palette_[0] : palette_[0x10U | (shifted & 0x0FU)];
    } else {
      entry = palette_[paletteIndex(static_cast<std::uint16_t>(0x10 | ((spriteAttribute & 3) << 2) | spritePixel))];
    }
  } else {
    entry = palette_[paletteIndex(static_cast<std::uint16_t>((backgroundPalette << 2) | backgroundPixel))];
  }
  entry = static_cast<std::uint8_t>(entry & ((mask_ & MASK_GRAYSCALE) != 0 ? 0x30 : 0x3F));
  const std::size_t emphasis = static_cast<std::size_t>(mask_ >> 5) & 7;
  frameBuffers_[static_cast<std::size_t>(drawIndex_)][static_cast<std::size_t>(line) * FRAME_WIDTH + static_cast<std::size_t>(x)] = rgbaTable_[emphasis * 64 + entry];
}

}  // namespace ember::logic::hardware
