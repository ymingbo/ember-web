/*
 * ------------------------------------------------------------------------
 * 名称：printer-text.hpp
 * 说明：文本转位图的打印适配：把收到的 ASCII 文本排成行，按行用 ESC/P 位图命令发给并口打印机。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-25
 * 备注：虚拟针式打印机（PrinterEscp）只认位图，不会自己画文字；本模块给只会送文本的接口卡用：
 *       可打印字符（$20-$7E）每个占 8 列（7 列字形加 1 列间隔，字模取自 8×8 字库，位 0 在最左），
 *       一行最多 80 个字符，满了自动换行；一行用 ESC * 1（120×72 点/英寸，8 针）发出，
 *       一列一个字节、位 7 在最上；回车（$0D）把已排好的行发出，换行（$0A）发一个换行，
 *       换页（$0C）先发出已排好的行再发换页，其余控制字符忽略；
 *       本模块不持有并口的所有权，并口必须活到本对象销毁。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "contracts/hardware/parallel-port.hpp"

namespace ember::logic::hardware {

/** 文本转位图的打印适配。 */
class PrinterText final {
 public:
  /** 一行最多的字符数。 */
  static constexpr std::size_t LINE_CHARACTERS = 80;
  /** 每个字符占的列数。 */
  static constexpr std::size_t CHARACTER_COLUMNS = 8;

  /** @param port 输出位图与控制字符的并口。 */
  explicit PrinterText(contracts::hardware::ParallelPort& port);

  /** 收到一个文本字节（高位忽略）。 */
  void write(std::uint8_t value);

  /** 把已排好的行发出（没有内容时什么也不做）。 */
  void flushLine();

  /** 丢弃已排好的行（复位时用）。 */
  void clear() { columns_.clear(); }

 private:
  contracts::hardware::ParallelPort& port_;
  /** 当前行的列数据：一列一个字节。 */
  std::vector<std::uint8_t> columns_;
};

}  // namespace ember::logic::hardware
