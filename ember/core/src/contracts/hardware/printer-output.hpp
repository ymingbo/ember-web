/*
 * ------------------------------------------------------------------------
 * 名称：printer-output.hpp
 * 说明：硬件契约：打印机的输出与控制。虚拟针式打印机把画好的一页交给输出方（保存成图片），控制方可以查询当前页的状态、立即输出当前页。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-15
 * 备注：一页是 1 位每像素的位图：每行按字节补齐，高位在左，1 表示黑点；输出方在模拟线程上被调用，不要长时间占用。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <cstdint>
#include <vector>

namespace ember::contracts::hardware {

/** 打印机输出的一页。 */
struct PrintedPage {
  /** 宽度（像素）。 */
  unsigned width{0};
  /** 高度（像素）。 */
  unsigned height{0};
  /** 位图：每行 (width + 7) / 8 字节，高位在左，1 表示黑点。 */
  std::vector<std::uint8_t> rows;
};

/** 输出方：收到打印机输出的一页。 */
class PrinterOutput {
 public:
  virtual ~PrinterOutput() = default;

  /** 收到一页（页上至少有一个点）。 */
  virtual void outputPage(PrintedPage page) = 0;
};

/** 打印机的控制：界面与调试服务经它查询和操作当前页。 */
class PrinterControl {
 public:
  virtual ~PrinterControl() = default;

  /** 当前页上是否已经有点。 */
  [[nodiscard]] virtual bool pageHasContent() const = 0;

  /** 自这台打印机创建以来输出过的页数。 */
  [[nodiscard]] virtual std::uint64_t pagesOutput() const = 0;

  /** 立即输出当前页，等同打印机收到换页；当前页没有内容时没有效果。 */
  virtual void flushPage() = 0;
};

}  // namespace ember::contracts::hardware
