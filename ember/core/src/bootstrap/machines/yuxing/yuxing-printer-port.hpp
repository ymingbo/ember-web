/*
 * ------------------------------------------------------------------------
 * 名称：yuxing-printer-port.hpp
 * 说明：裕兴学习机的打印机接口：打印机接在 $4016 手柄口的扩展线上，程序逐位送出一个字节，这里把它拼好后交给并口上的打印机。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-22
 * 备注：写 $4016：位 0 是数据线；位 1 是时钟线，每次上升沿把位 0 移入移位寄存器（高位在先）；位 2 是锁存线，平时为高，下降沿时把移位寄存器里的字节交给打印机；
 *       读 $4016 位 1：最近一次写入的值恰好是 $06（程序询问打印机是否就绪时写的空闲字）时，打印机就绪（接着打印机且不忙）为 1，其余时候不叠加；
 *       $4016 同时是手柄、鼠标与磁带的口，它们的写入也经过这里，只有上面说的沿变化才起作用；
 *       只由 yuxing.cpp 创建并接到输入口上。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <cstdint>

#include "contracts/hardware/parallel-port.hpp"

namespace ember::bootstrap {

/** 裕兴学习机的打印机接口。 */
class YuxingPrinterPort final {
 public:
  /** @param printer 并口打印机，必须活到本对象销毁；为空时没有接打印机。 */
  explicit YuxingPrinterPort(contracts::hardware::ParallelPort* printer) : printer_(printer) {}

  /** 写 $4016。 */
  void write(std::uint8_t value);

  /** 读 $4016 时位 1 是否要叠加打印机就绪。 */
  [[nodiscard]] bool ready() const;

  /** 复位（上电或软复位）：忘掉写入的历史。 */
  void reset();

 private:
  contracts::hardware::ParallelPort* printer_;
  /** 最近一次写入的值。 */
  std::uint8_t last_{0};
  /** 已经移入的位。 */
  std::uint8_t shift_{0};
};

}  // namespace ember::bootstrap
