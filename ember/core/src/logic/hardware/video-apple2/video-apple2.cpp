/*
 * ------------------------------------------------------------------------
 * 名称：video-apple2.cpp
 * 说明：苹果 II 类视频生成器的实现。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-25
 * 备注：无
 * ------------------------------------------------------------------------
 */

#include "logic/hardware/video-apple2/video-apple2.hpp"

#include <algorithm>
#include <array>

#include "logic/hardware/video-apple2/apple2-font.hpp"

namespace ember::logic::hardware {

namespace {

namespace hw = contracts::hardware;

/** 文本屏每行的字符数，字符的点宽与行高。 */
constexpr int TEXT_COLUMNS = 40;
constexpr int GLYPH_WIDTH = 7;
constexpr int GLYPH_HEIGHT = 8;
/** 苹果点的宽。 */
constexpr int DOT_COLUMNS = 280;
/** 混合模式下文本占的起始行。 */
constexpr int MIXED_FIRST_TEXT_ROW = 20;
/** 闪烁字符每隔多少帧翻转一次。 */
constexpr std::uint64_t FLASH_FRAMES = 16;

constexpr std::uint16_t TEXT_PAGE_1 = 0x0400;
constexpr std::uint16_t TEXT_PAGE_2 = 0x0800;
constexpr std::uint16_t HIRES_PAGE_1 = 0x2000;
constexpr std::uint16_t HIRES_PAGE_2 = 0x4000;

constexpr std::uint32_t rgb(std::uint32_t red, std::uint32_t green, std::uint32_t blue) { return 0xFF000000U | (blue << 16) | (green << 8) | red; }

constexpr std::uint32_t BLACK = rgb(0, 0, 0);
constexpr std::uint32_t WHITE = rgb(255, 255, 255);

/** 灰度：用亮度公式算出的灰色。 */
constexpr std::uint32_t grayOf(std::uint32_t color) {
  const std::uint32_t red = color & 0xFFU;
  const std::uint32_t green = (color >> 8) & 0xFFU;
  const std::uint32_t blue = (color >> 16) & 0xFFU;
  const std::uint32_t level = (red * 30 + green * 59 + blue * 11) / 100;
  return rgb(level, level, level);
}

/** 低分辨率的 16 种颜色。 */
constexpr std::array<std::uint32_t, 16> LORES_COLORS = {rgb(0, 0, 0),       rgb(227, 30, 96),  rgb(96, 78, 189),   rgb(255, 68, 253), rgb(0, 163, 96),    rgb(156, 156, 156), rgb(20, 207, 253), rgb(208, 195, 255),
                                                        rgb(96, 114, 3),    rgb(255, 106, 60), rgb(156, 156, 156), rgb(255, 160, 208), rgb(20, 245, 60), rgb(208, 221, 141), rgb(114, 255, 208), rgb(255, 255, 255)};

/** 高分辨率每个苹果点占两个半点（色彩副载波每周期四个半点），一行 560 个半点。 */
constexpr int HALF_DOTS_PER_LINE = DOT_COLUMNS * 2;
constexpr int HALF_DOTS_PER_BYTE = GLYPH_WIDTH * 2;
/** 单独的亮点按起点相位（对四取余）显示的颜色：紫、蓝、绿、橙。 */
constexpr std::array<std::uint32_t, 4> PHASE_COLORS = {LORES_COLORS[3], LORES_COLORS[6], LORES_COLORS[12], LORES_COLORS[9]};

/** 文本第 row 行（0 到 23）的起始地址偏移：每 8 行一组，组内每行间隔 $80，每组间隔 $28。 */
constexpr std::uint16_t textRowOffset(int row) { return static_cast<std::uint16_t>((row & 7) * 0x80 + (row >> 3) * 0x28); }

/** 高分辨率第 line 行（0 到 191）的起始地址偏移。 */
constexpr std::uint16_t hiresLineOffset(int line) { return static_cast<std::uint16_t>((line & 7) * 0x400 + ((line >> 3) & 7) * 0x80 + (line >> 6) * 0x28); }

}  // namespace

VideoApple2::VideoApple2(hw::Scheduler& scheduler, const Apple2VideoSource& source, hw::MasterTime lineTicks)
    : scheduler_(scheduler), source_(source), lineTicks_(lineTicks), pixels_(static_cast<std::size_t>(FRAME_WIDTH) * FRAME_HEIGHT, BLACK) {}

void VideoApple2::reset(bool power) {
  if (event_ != hw::NO_EVENT) scheduler_.cancel(event_);
  if (power) {
    std::fill(pixels_.begin(), pixels_.end(), BLACK);
    frameCount_ = 0;
  }
  frameStart_ = scheduler_.now();
  nextLine_ = 0;
  event_ = scheduler_.scheduleAt(frameStart_ + lineTicks_, *this, 0);
}

void VideoApple2::halt() {
  if (event_ != hw::NO_EVENT) scheduler_.cancel(event_);
  event_ = hw::NO_EVENT;
}

hw::FrameBuffer VideoApple2::frame() const {
  if (frameCount_ == 0) return hw::FrameBuffer{};
  return hw::FrameBuffer{FRAME_WIDTH, FRAME_HEIGHT, std::span<const std::uint32_t>(pixels_)};
}

int VideoApple2::scanline() const {
  const hw::MasterTime elapsed = std::max<hw::MasterTime>(0, scheduler_.now() - frameStart_);
  return std::min<int>(SCANLINES_PER_FRAME - 1, static_cast<int>(elapsed / lineTicks_));
}

std::vector<std::uint32_t> VideoApple2::loresColors() { return std::vector<std::uint32_t>(LORES_COLORS.begin(), LORES_COLORS.end()); }

void VideoApple2::onEvent(hw::MasterTime when, std::uint32_t /*tag*/) {
  // 一行扫描完时，按此刻的显示开关与主存画这一行。
  if (nextLine_ < DISPLAY_SCANLINES) renderLine(nextLine_);
  ++nextLine_;
  if (nextLine_ == SCANLINES_PER_FRAME) {
    nextLine_ = 0;
    ++frameCount_;
    frameStart_ = when;
  }
  event_ = scheduler_.scheduleAt(when + lineTicks_, *this, 0);
}

void VideoApple2::renderNow() {
  for (int line = 0; line < DISPLAY_SCANLINES; ++line) renderLine(line);
}

void VideoApple2::renderLine(int line) {
  const Apple2VideoMode mode = source_.videoMode();
  colorEnabled_ = mode.colorEnabled;
  const std::uint16_t textPage = mode.page2 ? TEXT_PAGE_2 : TEXT_PAGE_1;
  if (mode.text || (mode.mixed && line >= MIXED_FIRST_TEXT_ROW * GLYPH_HEIGHT)) renderTextLine(line, textPage);
  else if (mode.hires) renderHiresLine(line, mode.page2 ? HIRES_PAGE_2 : HIRES_PAGE_1);
  else renderLoresLine(line, textPage);
}

void VideoApple2::putDot(int x, int y, std::uint32_t color) {
  const std::size_t index = static_cast<std::size_t>(y) * 2 * FRAME_WIDTH + static_cast<std::size_t>(x) * 2;
  pixels_[index] = color;
  pixels_[index + 1] = color;
  pixels_[index + FRAME_WIDTH] = color;
  pixels_[index + FRAME_WIDTH + 1] = color;
}

void VideoApple2::renderTextLine(int line, std::uint16_t pageBase) {
  const bool flashOff = ((frameCount_ / FLASH_FRAMES) & 1U) != 0;
  const int row = line / GLYPH_HEIGHT;
  const int glyphLine = line % GLYPH_HEIGHT;
  for (int column = 0; column < TEXT_COLUMNS; ++column) {
    const std::uint8_t value = source_.readVideoByte(static_cast<std::uint16_t>(pageBase + textRowOffset(row) + column));
    // 字符码：$E0 以上显示小写字母与符号，其余取低 6 位显示成 $20-$5F 的字符。
    std::uint8_t ascii = 0;
    if (value >= 0xE0) {
      ascii = value & 0x7F;
    } else {
      const std::uint8_t index = value & 0x3F;
      ascii = index < 0x20 ? static_cast<std::uint8_t>(index + 0x40) : index;
    }
    const bool inverted = value < 0x40 || (value < 0x80 && flashOff);
    const std::size_t glyph = static_cast<std::size_t>(std::clamp<int>(ascii, 0x20, 0x7F) - 0x20) * apple2::FONT_GLYPH_HEIGHT;
    const std::uint8_t bits = apple2::TEXT_FONT[glyph + static_cast<std::size_t>(glyphLine)];
    for (int dot = 0; dot < GLYPH_WIDTH; ++dot) {
      const bool lit = (((bits >> dot) & 1U) != 0) != inverted;
      putDot(column * GLYPH_WIDTH + dot, line, lit ? WHITE : BLACK);
    }
  }
}

void VideoApple2::renderLoresLine(int line, std::uint16_t pageBase) {
  const int row = line / GLYPH_HEIGHT;
  const bool upperHalf = line % GLYPH_HEIGHT < 4;
  for (int column = 0; column < TEXT_COLUMNS; ++column) {
    const std::uint8_t value = source_.readVideoByte(static_cast<std::uint16_t>(pageBase + textRowOffset(row) + column));
    const std::uint8_t index = upperHalf ? (value & 0x0F) : (value >> 4);
    const std::uint32_t color = colorEnabled_ ? LORES_COLORS[index] : grayOf(LORES_COLORS[index]);
    for (int dot = 0; dot < GLYPH_WIDTH; ++dot) putDot(column * GLYPH_WIDTH + dot, line, color);
  }
}

void VideoApple2::renderHiresLine(int line, std::uint16_t pageBase) {
  std::array<std::uint8_t, HALF_DOTS_PER_LINE> signal{};
  std::array<std::uint32_t, HALF_DOTS_PER_LINE> colors{};
  const std::uint16_t base = static_cast<std::uint16_t>(pageBase + hiresLineOffset(line));
  // 视频信号：每个点两个半点；位 7 为 1 的字节整体延后一个半点，
  // 第一个半点保持前一字节的最后一个半点。
  std::uint8_t previousLast = 0;
  for (int column = 0; column < TEXT_COLUMNS; ++column) {
    const std::uint8_t value = source_.readVideoByte(static_cast<std::uint16_t>(base + column));
    std::array<std::uint8_t, HALF_DOTS_PER_BYTE> halves{};
    for (int dot = 0; dot < GLYPH_WIDTH; ++dot) halves[static_cast<std::size_t>(dot * 2)] = halves[static_cast<std::size_t>(dot * 2 + 1)] = (value >> dot) & 1U;
    const bool delayed = (value & 0x80) != 0;
    for (int half = 0; half < HALF_DOTS_PER_BYTE; ++half) {
      const std::uint8_t level = !delayed ? halves[static_cast<std::size_t>(half)] : (half == 0 ? previousLast : halves[static_cast<std::size_t>(half - 1)]);
      signal[static_cast<std::size_t>(column * HALF_DOTS_PER_BYTE + half)] = level;
    }
    previousLast = halves[HALF_DOTS_PER_BYTE - 1];
  }
  // 连续亮三个半点以上是白；两个半点以内的单独亮点按起点相位取色，
  // 两个同色的单独亮点之间只隔两个半点以内时，中间补成同色，色块边缘不加色边。
  colors.fill(BLACK);
  std::uint32_t previousColor = BLACK;
  int previousEnd = 0;
  int x = 0;
  while (x < HALF_DOTS_PER_LINE) {
    if (signal[static_cast<std::size_t>(x)] == 0) {
      ++x;
      continue;
    }
    int end = x;
    while (end < HALF_DOTS_PER_LINE && signal[static_cast<std::size_t>(end)] != 0) ++end;
    const std::uint32_t color = (!colorEnabled_ || end - x >= 3) ? WHITE : PHASE_COLORS[static_cast<std::size_t>(x & 3)];
    std::fill(colors.begin() + x, colors.begin() + end, color);
    if (color != WHITE && color == previousColor && x - previousEnd <= 2) std::fill(colors.begin() + previousEnd, colors.begin() + x, color);
    previousColor = color;
    previousEnd = end;
    x = end;
  }
  const std::size_t row = static_cast<std::size_t>(line) * 2 * FRAME_WIDTH;
  for (int pixel = 0; pixel < HALF_DOTS_PER_LINE; ++pixel) {
    pixels_[row + static_cast<std::size_t>(pixel)] = colors[static_cast<std::size_t>(pixel)];
    pixels_[row + FRAME_WIDTH + static_cast<std::size_t>(pixel)] = colors[static_cast<std::size_t>(pixel)];
  }
}

}  // namespace ember::logic::hardware
