/*
 * ------------------------------------------------------------------------
 * 名称：parallel-port.hpp
 * 说明：硬件契约：并口。主机把数据与控制线送给打印机，打印机把状态线送回主机；信号都用引脚电平表示（true 为高电平）。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-15
 * 备注：引脚名沿用 PC 并口的叫法：带 N 的是低电平有效。没有接打印机的口由 NullParallelPort 实现；
 *       状态线的电平由打印机决定，寄存器怎么读到它们由主机型号的并口模块决定。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <cstdint>

namespace ember::contracts::hardware {

/** 并口。 */
class ParallelPort {
 public:
  virtual ~ParallelPort() = default;

  /** 主机把 8 位数据线设为 value；6576 写 $4025 时芯片自动产生选通，所以打印机在这时就收下这个字节。 */
  virtual void writeData(std::uint8_t value) = 0;

  /** #Strobe 线的电平：只记录电平；6576 的数据在写数据寄存器时就被打印机收下。 */
  virtual void setStrobeN(bool high) = 0;

  /** #Select Printer 线的电平：为低表示选中打印机。 */
  virtual void setSelectPrinterN(bool high) = 0;

  /** #Linefeed 线的电平：为低表示收到回车时自动换行。 */
  virtual void setLineFeedN(bool high) = 0;

  /** Reset 线的电平：为高时打印机复位。 */
  virtual void setReset(bool high) = 0;

  /** Ack 线的电平。 */
  [[nodiscard]] virtual bool ack() const = 0;

  /** Paper Out 线的电平：为高表示缺纸。 */
  [[nodiscard]] virtual bool paperOut() const = 0;

  /** Select 线的电平：为高表示打印机在线。 */
  [[nodiscard]] virtual bool select() const = 0;

  /** Error 线的电平：为高表示出错。 */
  [[nodiscard]] virtual bool error() const = 0;

  /** #Busy 线的电平：为低表示忙。 */
  [[nodiscard]] virtual bool busyN() const = 0;

  /** 复位（断电或硬复位）：丢弃还没有输出的内容。 */
  virtual void reset() = 0;
};

/** 没有接打印机的并口：输入引脚被上拉或下拉，输出引脚的变化被忽略。 */
class NullParallelPort final : public ParallelPort {
 public:
  void writeData(std::uint8_t /*value*/) override {}
  void setStrobeN(bool /*high*/) override {}
  void setSelectPrinterN(bool /*high*/) override {}
  void setLineFeedN(bool /*high*/) override {}
  void setReset(bool /*high*/) override {}
  [[nodiscard]] bool ack() const override { return true; }
  [[nodiscard]] bool paperOut() const override { return true; }
  [[nodiscard]] bool select() const override { return true; }
  [[nodiscard]] bool error() const override { return false; }
  [[nodiscard]] bool busyN() const override { return false; }
  void reset() override {}
};

}  // namespace ember::contracts::hardware
