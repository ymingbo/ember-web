/*
 * ------------------------------------------------------------------------
 * 名称：png-printer-page-store.hpp
 * 说明：打印页保存的实现：把一页编码成黑白两色的 PNG（1 位灰度，miniz 压缩），写进首选项里设置的目录。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-15
 * 备注：文件先写成临时文件再改名；同名文件已存在时在文件名后加 -2、-3……（同一秒输出多页）。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <cstdint>
#include <vector>

#include "contracts/media/printer-page-store.hpp"

namespace ember::infra::media {

/** PNG 格式的打印页保存。 */
class PngPrinterPageStore final : public contracts::media::PrinterPageStore {
 public:
  std::string save(const std::string& directory, const std::string& baseName, const contracts::hardware::PrintedPage& page) override;

  /** 把一页编码成 PNG（1 位灰度，0 为黑）；页上的 1 表示黑点。 */
  [[nodiscard]] static std::vector<std::uint8_t> encode(const contracts::hardware::PrintedPage& page);
};

}  // namespace ember::infra::media
