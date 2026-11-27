/*
 * ------------------------------------------------------------------------
 * 名称：storage-region.hpp
 * 说明：硬件契约：存储区。一块 ROM 或 RAM，有唯一的所有者，只经所有者访问。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * 备注：存储区只负责字节存取，不负责地址译码；译码由总线、芯片或 mapper 完成。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace ember::contracts::hardware {

/** 存储区种类。 */
enum class StorageKind : std::uint8_t {
  /** 只读存储：拒绝写入。 */
  Rom,
  /** 可读写存储。 */
  Ram
};

/** 存储区。偏移必须小于容量，越界是调用方的错误，实现报错。 */
class StorageRegion {
 public:
  virtual ~StorageRegion() = default;

  /** 种类。 */
  [[nodiscard]] virtual StorageKind kind() const = 0;

  /** 容量（字节）。 */
  [[nodiscard]] virtual std::size_t size() const = 0;

  /** 读一个字节。 */
  [[nodiscard]] virtual std::uint8_t read(std::size_t offset) const = 0;

  /**
   * 写一个字节。
   * @return 写入成功返回 true；ROM 拒绝写入，返回 false，内容不变。
   */
  virtual bool write(std::size_t offset, std::uint8_t value) = 0;

  /** 从偏移处批量读取，填满 out；读取范围超出容量时报错。 */
  virtual void readBlock(std::size_t offset, std::span<std::uint8_t> out) const = 0;
};

}  // namespace ember::contracts::hardware
