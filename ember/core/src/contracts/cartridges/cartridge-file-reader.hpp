/*
 * ------------------------------------------------------------------------
 * 名称：cartridge-file-reader.hpp
 * 说明：cartridges 契约：读取用户选定的 NES 文件。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * 备注：卡带文件是用户自己的外部文件，路径由主进程根据用户的选择给出（见 docs/01-软件架构设计.md 第 6、9 节）；实现在 infra/cartridges。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ember::contracts::cartridges {

/** 卡带文件读取。 */
class CartridgeFileReader {
 public:
  virtual ~CartridgeFileReader() = default;

  /**
   * 读取整个文件。
   * @param path 文件路径（UTF-8）。
   * @throws shared::CoreError cartridges.fileNotFound：文件不存在；cartridges.fileUnreadable：不是普通文件或读取失败；
   *         cartridges.unsupportedFormat：文件大得不可能是 NES 卡带。
   */
  [[nodiscard]] virtual std::vector<std::uint8_t> read(const std::string& path) const = 0;
};

}  // namespace ember::contracts::cartridges
