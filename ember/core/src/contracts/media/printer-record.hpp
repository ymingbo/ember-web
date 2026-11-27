/*
 * ------------------------------------------------------------------------
 * 名称：printer-record.hpp
 * 说明：media 契约：打印机输出的记录：本次运行以来输出的页数与最近一页的文件名。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-15
 * 备注：记录在模拟线程上更新、在请求线程上读取，实现自己保证线程安全。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <cstdint>
#include <string>

namespace ember::contracts::media {

/** 打印机输出的记录。 */
struct PrinterRecordSnapshot {
  /** 已输出（已保存成文件）的页数。 */
  std::int64_t pages{0};
  /** 最近一页的文件名；还没有时为空。 */
  std::string lastFile;
  /** 图片保存的目录（读取时按首选项取当前值）。 */
  std::string directory;
};

/** 打印机输出的记录。 */
class PrinterRecord {
 public:
  virtual ~PrinterRecord() = default;

  /** 当前的记录。 */
  [[nodiscard]] virtual PrinterRecordSnapshot snapshot() const = 0;
};

}  // namespace ember::contracts::media
