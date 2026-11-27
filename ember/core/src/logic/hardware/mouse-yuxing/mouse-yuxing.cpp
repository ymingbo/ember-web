/*
 * ------------------------------------------------------------------------
 * 名称：mouse-yuxing.cpp
 * 说明：裕兴鼠标的实现：数据包的编码与逐位移出。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-21
 * ------------------------------------------------------------------------
 */

#include "logic/hardware/mouse-yuxing/mouse-yuxing.hpp"

#include <algorithm>

namespace ember::logic::hardware {

namespace {

/** 写 $4016 的位：位 0 是时钟，位 2 为 1 时不移位。 */
constexpr std::uint8_t WRITE_CLOCK = 0x01;
constexpr std::uint8_t WRITE_HOLD = 0x04;
/** 数据包第 1 个字节的固定位与按键位。 */
constexpr std::uint8_t PACKET_BASE = 0x40;
constexpr std::uint8_t PACKET_LEFT = 0x20;
constexpr std::uint8_t PACKET_RIGHT = 0x10;
/** 位移的范围（8 位有符号数）。 */
constexpr int DELTA_MAX = 127;
constexpr int DELTA_MIN = -128;
/** 一个字节里的数据位数。 */
constexpr unsigned DATA_BITS = 7;

}  // namespace

void MouseYuxing::reset() {
  count_ = 0;
  position_ = 0;
  clock_ = false;
  pendingX_ = 0;
  pendingY_ = 0;
  frameX_ = 0;
  frameY_ = 0;
  buttons_ = 0;
  frameButtons_ = 0;
  /* 指针状态是页面状态，不随 ROM 复位清零 ——
     清了要等用户再动一次鼠标才有 mousemove，
     这段时间里所有点击都无效。 */
}

void MouseYuxing::write(std::uint8_t value) {
  const bool clockHigh = (value & WRITE_CLOCK) != 0;
  if ((value & WRITE_HOLD) == 0 && !clock_ && clockHigh) {
    if (position_ >= count_) loadPacket();
    else ++position_;
  }
  clock_ = clockHigh;
}

std::uint8_t MouseYuxing::read() const {
  if (!clock_ || position_ >= count_) return 0;
  return bits_[position_] ? 0 : 1;
}

void MouseYuxing::move(int dx, int dy, std::uint8_t buttons) {
  pendingX_ += dx;
  pendingY_ += dy;
  buttons_ = buttons;
}

void MouseYuxing::setPointer(int absX, int absY, std::uint8_t buttons) {
  /* 只记录绝对值与按键，位移在 syncFrame() 里按"目标 - 当前"算。
     这样单帧位移再大也不会丢：没追完的下一帧接着追。 */
  targetX_ = std::clamp(absX, 0, 255);
  targetY_ = std::clamp(absY, 0, 239);
  buttons_ = buttons;
  pointerActive_ = true;
}

void MouseYuxing::syncFrame() {
  /* 每帧只做两件事：让虚拟指针向目标挪一步、刷新按键。
     **不碰 pointerOld** —— 那是"上次装包时的位置"，
     由 loadPacket() 独占更新。 */
  if (pointerActive_) {
    int dx = targetX_ - pointerX_;
    int dy = targetY_ - pointerY_;
    if (dx > MAX_STEP) dx = MAX_STEP; else if (dx < -MAX_STEP) dx = -MAX_STEP;
    if (dy > MAX_STEP) dy = MAX_STEP; else if (dy < -MAX_STEP) dy = -MAX_STEP;
    pointerX_ += dx;
    pointerY_ += dy;
  }
  frameButtons_ = buttons_;
}

void MouseYuxing::loadPacket() {
  /* 装包时结算位移，然后**立刻把当前位置记成 old**。
   *
   * old 只在这里更新，不在 syncFrame() 更新：
   * ROM 可能隔一帧、甚至隔几帧才读一轮包，如果 old 每帧都被刷新，
   * 两次装包之间积累的位移只剩最后一帧的那一点 —— 指针永远差一段，
   * 表现就是"有追的动作但追不到位置"。
   *
   * 现在不管 ROM 隔多久读一次，两次装包之间的位移都完整累加进这一包。 */
  int dx = pointerX_ - pointerOldX_;
  int dy = pointerY_ - pointerOldY_;
  /* 7 位编码装不下大位移：每包最多报 MAX_STEP，
     剩下的留在 pointerOldX_ 的差值里，下一包继续。 */
  if (dx > MAX_STEP) dx = MAX_STEP; else if (dx < -MAX_STEP) dx = -MAX_STEP;
  if (dy > MAX_STEP) dy = MAX_STEP; else if (dy < -MAX_STEP) dy = -MAX_STEP;
  frameX_ = dx;
  frameY_ = dy;
  /* 只推进"已报位置"，不是直接跳到当前指针位置。 */
  pointerOldX_ += dx;
  pointerOldY_ += dy;

  std::uint8_t first = PACKET_BASE;
  if ((frameButtons_ & BUTTON_LEFT) != 0) first = static_cast<std::uint8_t>(first | PACKET_LEFT);
  if ((frameButtons_ & BUTTON_RIGHT) != 0) first = static_cast<std::uint8_t>(first | PACKET_RIGHT);
  first = static_cast<std::uint8_t>(first | ((frameX_ >> 4) & 0x0C) | ((frameY_ >> 6) & 0x03));
  unsigned index = 0;
  appendByte(first, index);
  appendByte(static_cast<std::uint8_t>(frameY_ & 0x3F), index);
  appendByte(static_cast<std::uint8_t>(frameX_ & 0x3F), index);
  count_ = index;
  position_ = 0;
}

void MouseYuxing::appendByte(std::uint8_t data, unsigned& index) {
  bits_[index++] = false;
  for (unsigned bit = 0; bit < DATA_BITS; ++bit) {
    bits_[index++] = (data & 0x40U) != 0;
    data = static_cast<std::uint8_t>(data << 1);
  }
  bits_[index++] = true;
}

}  // namespace ember::logic::hardware
