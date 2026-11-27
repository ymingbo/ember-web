/*
 * ------------------------------------------------------------------------
 * 名称：storage-block.hpp
 * 说明：卡带上的存储区实现：一块 ROM 或 RAM，数据在创建时从 NES 文件复制或清零。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * 备注：窗口访问走不检查的 byteAt/writeAt（调用方保证偏移在容量内）；经契约的 read/write 访问时检查偏移，越界报错；
 *       ROM 拒绝写入；带电池的 RAM 复位时保持内容（存档文件的载入与保存在以后的阶段）。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include "contracts/hardware/hardware-error.hpp"
#include "contracts/hardware/storage-region.hpp"
#include "shared/errors/core-error.hpp"

namespace ember::infra::mappers {

/** 存储区。 */
class StorageBlock final : public contracts::hardware::StorageRegion {
 public:
  /**
   * @param kind 种类。
   * @param bytes 内容；容量可以为 0（文件里没有这一部分又没有后备存储时）。
   * @param battery 是否带电池，只对 RAM 有意义。
   */
  StorageBlock(contracts::hardware::StorageKind kind, std::vector<std::uint8_t> bytes, bool battery) : kind_(kind), battery_(battery), bytes_(std::move(bytes)) {}

  [[nodiscard]] contracts::hardware::StorageKind kind() const override { return kind_; }

  [[nodiscard]] std::size_t size() const override { return bytes_.size(); }

  [[nodiscard]] std::uint8_t read(std::size_t offset) const override {
    check(offset);
    return bytes_[offset];
  }

  bool write(std::size_t offset, std::uint8_t value) override {
    check(offset);
    return writeAt(offset, value);
  }

  void readBlock(std::size_t offset, std::span<std::uint8_t> out) const override {
    if (offset > bytes_.size() || out.size() > bytes_.size() - offset) throw shared::CoreError(contracts::hardware::ERROR_STORAGE_OUT_OF_RANGE, "读取范围超出存储区的容量");
    std::copy_n(bytes_.begin() + static_cast<std::ptrdiff_t>(offset), out.size(), out.begin());
  }

  /** 读一个字节，不检查偏移。 */
  [[nodiscard]] std::uint8_t byteAt(std::size_t offset) const noexcept { return bytes_[offset]; }

  /** 写一个字节，不检查偏移；ROM 拒绝写入并返回 false。 */
  bool writeAt(std::size_t offset, std::uint8_t value) noexcept {
    if (kind_ == contracts::hardware::StorageKind::Rom) return false;
    bytes_[offset] = value;
    return true;
  }

  /** 是否带电池。 */
  [[nodiscard]] bool battery() const noexcept { return battery_; }

  /** 上电复位：不带电池的 RAM 清零，ROM 与带电池的 RAM 保持。 */
  void powerCycle() noexcept {
    if (kind_ == contracts::hardware::StorageKind::Ram && !battery_) std::fill(bytes_.begin(), bytes_.end(), std::uint8_t{0});
  }

 private:
  void check(std::size_t offset) const {
    if (offset >= bytes_.size()) throw shared::CoreError(contracts::hardware::ERROR_STORAGE_OUT_OF_RANGE, "访问超出存储区的容量");
  }

  contracts::hardware::StorageKind kind_;
  bool battery_;
  std::vector<std::uint8_t> bytes_;
};

}  // namespace ember::infra::mappers
