/*
 * ------------------------------------------------------------------------
 * 名称：disk-cartridge-file-reader.hpp
 * 说明：卡带文件读取的实现：从本机磁盘读取整个文件。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * ------------------------------------------------------------------------
 */

#pragma once

#include <cstddef>

#include "contracts/cartridges/cartridge-file-reader.hpp"

namespace ember::infra::cartridges {

/** 读取磁盘上的 NES 文件。 */
class DiskCartridgeFileReader final : public contracts::cartridges::CartridgeFileReader {
 public:
  /** 文件大小的上限（字节）：最大的 NES 2.0 卡带也远小于此，超过的不是卡带，不读入内存。 */
  static constexpr std::size_t MAX_FILE_SIZE = 64U * 1024U * 1024U;

  [[nodiscard]] std::vector<std::uint8_t> read(const std::string& path) const override;
};

}  // namespace ember::infra::cartridges
