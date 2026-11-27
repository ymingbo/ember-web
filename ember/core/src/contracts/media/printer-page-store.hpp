/*
 * ------------------------------------------------------------------------
 * 名称：printer-page-store.hpp
 * 说明：media 契约：把打印机输出的一页保存成图片文件。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-15
 * 备注：实现在 infra/media（PNG 编码用 miniz）；保存位置由调用方给出，是首选项里设置的目录。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <string>

#include "contracts/hardware/printer-output.hpp"

namespace ember::contracts::media {

/** 打印页的保存。 */
class PrinterPageStore {
 public:
  virtual ~PrinterPageStore() = default;

  /**
   * 保存一页：目录不存在时创建；文件名是 baseName 加扩展名，已有同名文件时在后面加 -2、-3……；先写临时文件再改名。
   * @param directory 目录（UTF-8）。
   * @param baseName 文件名，不含扩展名。
   * @param page 一页。
   * @return 实际的文件名（含扩展名，不含路径）。
   * @throws shared::CoreError media.saveFailed：目录无法创建或文件无法写入。
   */
  virtual std::string save(const std::string& directory, const std::string& baseName, const hardware::PrintedPage& page) = 0;
};

}  // namespace ember::contracts::media
