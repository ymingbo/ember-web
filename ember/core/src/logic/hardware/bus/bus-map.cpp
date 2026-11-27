/*
 * ------------------------------------------------------------------------
 * 名称：bus-map.cpp
 * 说明：总线的地址译码与开放总线的实现。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * ------------------------------------------------------------------------
 */

#include "logic/hardware/bus/bus-map.hpp"

#include <algorithm>
#include <string>

#include "contracts/hardware/hardware-error.hpp"
#include "shared/errors/core-error.hpp"

namespace ember::logic::hardware {

namespace {

namespace hw = contracts::hardware;

/** 十六进制文本，用于错误说明。 */
std::string hex(std::uint32_t value) {
  static constexpr char DIGITS[] = "0123456789ABCDEF";
  std::string text;
  do {
    text.insert(text.begin(), DIGITS[value & 0xF]);
    value >>= 4;
  } while (value != 0);
  return "$" + std::string(text.size() < 4 ? 4 - text.size() : 0, '0') + text;
}

}  // namespace

BusMap::BusMap(unsigned addressBits) {
  if (addressBits < 1 || addressBits > 16) throw shared::CoreError(hw::ERROR_INVALID_ARGUMENT, "总线地址宽度必须在 1 到 16 位之间");
  addressMask_ = (std::uint32_t{1} << addressBits) - 1;
}

void BusMap::mount(std::uint16_t first, std::uint16_t last, hw::BusDevice& device) {
  if (first > last) throw shared::CoreError(hw::ERROR_BUS_RANGE_INVALID, "总线地址范围的起点 " + hex(first) + " 大于终点 " + hex(last));
  if (last > addressMask_) throw shared::CoreError(hw::ERROR_BUS_RANGE_INVALID, "总线地址范围 " + hex(first) + "-" + hex(last) + " 超出总线的地址宽度（最大 " + hex(addressMask_) + "）");

  // 第一个起点大于新范围起点的挂载；它的前一个才可能与新范围重叠。
  const auto next = std::upper_bound(mappings_.begin(), mappings_.end(), std::uint32_t{first}, [](std::uint32_t value, const Mapping& mapping) { return value < mapping.first; });
  const auto overlaps = [&](const Mapping& mapping) { return mapping.first <= last && first <= mapping.last; };
  if (next != mappings_.begin() && overlaps(*(next - 1))) {
    const Mapping& other = *(next - 1);
    throw shared::CoreError(hw::ERROR_BUS_RANGE_CONFLICT, "总线地址范围 " + hex(first) + "-" + hex(last) + " 与已挂载的 " + hex(other.first) + "-" + hex(other.last) + " 重叠");
  }
  if (next != mappings_.end() && overlaps(*next)) {
    throw shared::CoreError(hw::ERROR_BUS_RANGE_CONFLICT, "总线地址范围 " + hex(first) + "-" + hex(last) + " 与已挂载的 " + hex(next->first) + "-" + hex(next->last) + " 重叠");
  }
  mappings_.insert(next, Mapping{first, last, &device});
}

const BusMap::Mapping* BusMap::find(std::uint32_t address) const {
  // 第一个起点大于地址的挂载的前一个，才可能覆盖地址。
  const auto next = std::upper_bound(mappings_.begin(), mappings_.end(), address, [](std::uint32_t value, const Mapping& mapping) { return value < mapping.first; });
  if (next == mappings_.begin()) return nullptr;
  const Mapping& candidate = *(next - 1);
  return address <= candidate.last ? &candidate : nullptr;
}

std::uint8_t BusMap::read(std::uint16_t address) {
  const std::uint32_t masked = address & addressMask_;
  const Mapping* mapping = find(masked);
  if (mapping == nullptr) return latch_;
  const hw::BusRead result = mapping->device->read(static_cast<std::uint16_t>(masked));
  // 设备驱动的位取设备的值，其余的位保持总线上的旧值。
  latch_ = static_cast<std::uint8_t>((result.value & result.drivenMask) | (latch_ & ~result.drivenMask));
  if (probe_ != nullptr) probe_->onAccess(hw::AccessKind::Read, static_cast<std::uint16_t>(masked), latch_);
  return latch_;
}

void BusMap::write(std::uint16_t address, std::uint8_t value) {
  const std::uint32_t masked = address & addressMask_;
  latch_ = value;
  const Mapping* mapping = find(masked);
  if (mapping != nullptr) mapping->device->write(static_cast<std::uint16_t>(masked), value);
  if (probe_ != nullptr) probe_->onAccess(hw::AccessKind::Write, static_cast<std::uint16_t>(masked), value);
}

std::uint8_t BusMap::peek(std::uint16_t address) const {
  const std::uint32_t masked = address & addressMask_;
  const Mapping* mapping = find(masked);
  if (mapping == nullptr) return 0;
  const hw::BusRead result = mapping->device->peek(static_cast<std::uint16_t>(masked));
  return static_cast<std::uint8_t>(result.value & result.drivenMask);
}

}  // namespace ember::logic::hardware
