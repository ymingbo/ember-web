/*
 * ------------------------------------------------------------------------
 * 名称：yuxing-printer-port.cpp
 * 说明：裕兴学习机的打印机接口的实现。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-22
 * ------------------------------------------------------------------------
 */

#include "bootstrap/machines/yuxing/yuxing-printer-port.hpp"

namespace ember::bootstrap {

namespace {

constexpr std::uint8_t DATA_BIT = 0x01;
constexpr std::uint8_t CLOCK_BIT = 0x02;
constexpr std::uint8_t LATCH_BIT = 0x04;
/** 程序询问打印机是否就绪之前写出的空闲字：时钟线与锁存线为高。 */
constexpr std::uint8_t IDLE_WORD = 0x06;

}  // namespace

void YuxingPrinterPort::write(std::uint8_t value) {
  const std::uint8_t changed = static_cast<std::uint8_t>(last_ ^ value);
  if ((changed & CLOCK_BIT) != 0 && (value & CLOCK_BIT) != 0) shift_ = static_cast<std::uint8_t>((shift_ << 1) | (value & DATA_BIT));
  if ((changed & LATCH_BIT) != 0 && (value & LATCH_BIT) == 0 && printer_ != nullptr) printer_->writeData(shift_);
  last_ = value;
}

bool YuxingPrinterPort::ready() const { return last_ == IDLE_WORD && printer_ != nullptr && printer_->busyN(); }

void YuxingPrinterPort::reset() {
  last_ = 0;
  shift_ = 0;
}

}  // namespace ember::bootstrap
