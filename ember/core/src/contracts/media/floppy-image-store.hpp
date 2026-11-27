/*
 * ------------------------------------------------------------------------
 * 名称：floppy-image-store.hpp
 * 说明：media 契约：盘片镜像文件的读取与写回。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-15
 * 备注：镜像文件是用户自己的外部文件，路径由主进程根据用户的选择给出，或由调试服务给出（见 docs/04-调试服务.md 第 7 节）；实现在 infra/media。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace ember::contracts::media {

/** 盘片镜像文件的存取。 */
class FloppyImageStore {
 public:
  virtual ~FloppyImageStore() = default;

  /**
   * 读取整个镜像文件。
   * @param path 文件路径（UTF-8）。
   * @throws shared::CoreError media.fileNotFound：文件不存在；media.fileUnreadable：不是普通文件或读取失败；media.imageInvalid：空文件或超过大小上限。
   */
  [[nodiscard]] virtual std::vector<std::uint8_t> read(const std::string& path) const = 0;

  /**
   * 把镜像写回文件：先写临时文件再改名，失败时原文件不变。
   * @throws shared::CoreError media.saveFailed。
   */
  virtual void write(const std::string& path, const std::vector<std::uint8_t>& bytes) = 0;

  /**
   * 把一段字节写进镜像文件的指定位置，文件的其余部分与大小不变（盘片被写过的扇区直接落到文件里）。
   * @param offset 文件内的起始位置。
   * @throws shared::CoreError media.saveFailed：文件打不开、被改短或写入失败。
   */
  virtual void writeAt(const std::string& path, std::size_t offset, std::span<const std::uint8_t> bytes) = 0;
};

}  // namespace ember::contracts::media
