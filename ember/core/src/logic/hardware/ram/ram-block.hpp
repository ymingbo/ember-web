/*
 * ------------------------------------------------------------------------
 * 名称：ram-block.hpp
 * 说明：RAM 模块：一块可读写的存储区（RamBlock），以及把它按容量镜像挂到总线上的设备（MirroredRam）。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * 备注：经典 FC 主板上的 2K 内部 RAM 挂在 $0000-$1FFF，每 2K 镜像一次；存储区本身只管字节存取，镜像由 MirroredRam 完成。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "contracts/hardware/bus-device.hpp"
#include "contracts/hardware/hardware-error.hpp"
#include "contracts/hardware/storage-region.hpp"
#include "shared/errors/core-error.hpp"

namespace ember::logic::hardware {

/** 可读写的存储区，上电内容为全零。 */
class RamBlock final : public contracts::hardware::StorageRegion {
 public:
  /** @param size 容量（字节），必须大于 0。 */
  explicit RamBlock(std::size_t size) : bytes_(size, 0) {
    if (size == 0) throw shared::CoreError(contracts::hardware::ERROR_INVALID_ARGUMENT, "RAM 的容量必须大于 0");
  }

  [[nodiscard]] contracts::hardware::StorageKind kind() const override { return contracts::hardware::StorageKind::Ram; }

  [[nodiscard]] std::size_t size() const override { return bytes_.size(); }

  [[nodiscard]] std::uint8_t read(std::size_t offset) const override {
    checkOffset(offset);
    return bytes_[offset];
  }

  bool write(std::size_t offset, std::uint8_t value) override {
    checkOffset(offset);
    bytes_[offset] = value;
    return true;
  }

  void readBlock(std::size_t offset, std::span<std::uint8_t> out) const override {
    if (offset > bytes_.size() || out.size() > bytes_.size() - offset) throw shared::CoreError(contracts::hardware::ERROR_STORAGE_OUT_OF_RANGE, "读取范围超出 RAM 的容量");
    std::copy_n(bytes_.begin() + static_cast<std::ptrdiff_t>(offset), out.size(), out.begin());
  }

  /** 全部清为指定的值（上电或测试用）。 */
  void fill(std::uint8_t value) { std::fill(bytes_.begin(), bytes_.end(), value); }

 private:
  void checkOffset(std::size_t offset) const {
    if (offset >= bytes_.size()) throw shared::CoreError(contracts::hardware::ERROR_STORAGE_OUT_OF_RANGE, "访问超出 RAM 的容量");
  }

  std::vector<std::uint8_t> bytes_;
};

/** 把存储区按容量镜像到一段总线地址上：地址对容量取余后访问存储区。 */
class MirroredRam final : public contracts::hardware::BusDevice {
 public:
  /** @param region 被镜像的存储区，必须活到本对象销毁。 */
  explicit MirroredRam(contracts::hardware::StorageRegion& region) : region_(region) {}

  contracts::hardware::BusRead read(std::uint16_t address) override { return contracts::hardware::driveBus(region_.read(address % region_.size())); }

  void write(std::uint16_t address, std::uint8_t value) override { region_.write(address % region_.size(), value); }

  contracts::hardware::BusRead peek(std::uint16_t address) override { return read(address); }

 private:
  contracts::hardware::StorageRegion& region_;
};

}  // namespace ember::logic::hardware
