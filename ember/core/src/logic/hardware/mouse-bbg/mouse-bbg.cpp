/*
 * ------------------------------------------------------------------------
 * 名称：mouse-bbg.cpp
 * 说明：步步高鼠标的实现：接通检测、收命令、按帧发送应答与数据。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-16
 * ------------------------------------------------------------------------
 */

#include "logic/hardware/mouse-bbg/mouse-bbg.hpp"

namespace ember::logic::hardware {

namespace {

/** 读 $4016 连续这么多次之后鼠标接通。 */
constexpr unsigned CONNECT_READS = 9;
/** 命令一共收这么多位。 */
constexpr unsigned COMMAND_BITS = 22;
/** 一帧的位数。 */
constexpr unsigned FRAME_BITS = 32;
/** 写 $4016 开始收命令的值；收命令时位 2 为 1 的写入才带一位命令。 */
constexpr std::uint8_t WRITE_START_COMMAND = 0x05;
constexpr std::uint8_t WRITE_BIT_VALID = 0x04;
constexpr std::uint8_t WRITE_BIT_DATA = 0x01;
/** 命令与应答。 */
constexpr std::uint8_t COMMAND_READ_DATA = 0xEB;
constexpr std::uint8_t RESPONSE_ACKNOWLEDGE = 0xFA;
/** 读数据的第 1 个字节里的标志位。 */
constexpr std::uint8_t FLAG_Y_NEGATIVE = 0x10;
constexpr std::uint8_t FLAG_X_NEGATIVE = 0x20;
constexpr std::uint8_t FLAG_Y_OVERFLOW = 0x40;
constexpr std::uint8_t FLAG_X_OVERFLOW = 0x80;
/** 位移的范围（9 位有符号数）。 */
constexpr int DELTA_MAX = 255;
constexpr int DELTA_MIN = -256;

}  // namespace

void MouseBbg::reset() {
  active_ = false;
  state_ = State::Idle;
  statusReads_ = 0;
  commandBits_ = 0;
  command_ = 0;
  lineReads_ = 0;
  frame_ = 0;
  framesLeft_ = 0;
  data_.fill(0);
  deltaX_ = 0;
  deltaY_ = 0;
  buttons_ = 0;
}

void MouseBbg::noteStatusRead() {
  if (active_) return;
  if (++statusReads_ == CONNECT_READS) {
    active_ = true;
    state_ = State::Idle;
  }
}

void MouseBbg::write(std::uint8_t value) {
  statusReads_ = 0;
  if (!active_) return;
  if (state_ == State::Idle) {
    if (value == WRITE_START_COMMAND) {
      lineReads_ = 0;
      command_ = 0;
      commandBits_ = 0;
      state_ = State::ReceiveCommand;
    }
  } else if (state_ == State::ReceiveCommand) {
    if ((value & WRITE_BIT_VALID) == 0) return;
    // 命令位取反，低位在前。
    if ((value & WRITE_BIT_DATA) == 0) command_ |= std::uint32_t{1} << commandBits_;
    if (++commandBits_ == COMMAND_BITS) finishCommand();
  }
}

void MouseBbg::finishCommand() {
  const auto command = static_cast<std::uint8_t>(command_ & 0xFF);
  frame_ = encodeFrame(RESPONSE_ACKNOWLEDGE);
  framesLeft_ = 1;
  if (command == COMMAND_READ_DATA) {
    int dx = -deltaX_;
    int dy = deltaY_;
    deltaX_ = 0;
    deltaY_ = 0;
    std::uint8_t flags = 0;
    if (dx < 0) flags = static_cast<std::uint8_t>(flags | FLAG_X_NEGATIVE);
    if (dy < 0) flags = static_cast<std::uint8_t>(flags | FLAG_Y_NEGATIVE);
    if (dx > DELTA_MAX || dx < DELTA_MIN) {
      flags = static_cast<std::uint8_t>(flags | FLAG_X_OVERFLOW);
      dx = 0;
    }
    if (dy > DELTA_MAX || dy < DELTA_MIN) {
      flags = static_cast<std::uint8_t>(flags | FLAG_Y_OVERFLOW);
      dy = 0;
    }
    data_[0] = static_cast<std::uint8_t>(buttons_ | flags);
    data_[1] = static_cast<std::uint8_t>(dy);
    data_[2] = static_cast<std::uint8_t>(dx);
    framesLeft_ = 4;
  }
  // 命令 $F0 与不认识的命令都只回应答。
  lineReads_ = 0;
  commandBits_ = 0;
  state_ = State::Transmit;
}

std::uint8_t MouseBbg::readLine() {
  statusReads_ = 0;
  if (!active_) return 0;
  std::uint8_t bit = 0;
  switch (state_) {
    case State::Idle:
      bit = 1;
      break;
    case State::ReceiveCommand:
      if ((lineReads_ & 1U) == 0) bit = 1;
      ++lineReads_;
      break;
    case State::Transmit:
      if (lineReads_ < FRAME_BITS) {
        if ((frame_ & (std::uint32_t{1} << lineReads_)) != 0) bit = 1;
        ++lineReads_;
      } else {
        lineReads_ = 0;
        --framesLeft_;
        if (framesLeft_ > 0) {
          // 应答之后依次发 3 个数据字节。
          frame_ = encodeFrame(data_[3 - framesLeft_]);
        } else {
          active_ = false;
          state_ = State::Idle;
        }
      }
      break;
  }
  return bit;
}

void MouseBbg::move(int dx, int dy, std::uint8_t buttons) {
  deltaX_ += dx;
  deltaY_ += dy;
  buttons_ = buttons;
}

std::uint32_t MouseBbg::encodeFrame(std::uint8_t data) {
  // 起始 4 位：1、1、0、1。
  std::uint32_t frame = 0x0B;
  std::uint32_t mask = 0x10;
  bool parity = false;
  for (unsigned index = 0; index < 8; ++index) {
    if ((data & 1U) == 0) {
      frame |= mask;
      parity = !parity;
    }
    frame |= mask << 2;
    mask <<= 3;
    data = static_cast<std::uint8_t>(data >> 1);
  }
  if (parity) frame |= mask;
  mask <<= 2;
  frame |= mask;
  return frame;
}

}  // namespace ember::logic::hardware
