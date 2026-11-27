/*
 * ------------------------------------------------------------------------
 * 名称：bus-map.hpp
 * 说明：总线的地址译码与开放总线：保存地址范围到设备的挂载，按地址分发读写，并保存总线上最近一次的值。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * 备注：CPU 总线与 PPU 总线只是地址宽度不同，共用本类，见 mapped-bus.hpp。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <cstdint>
#include <vector>

#include "contracts/hardware/access-probe.hpp"
#include "contracts/hardware/bus-device.hpp"

namespace ember::logic::hardware {

/** 地址范围到设备的映射。 */
class BusMap {
 public:
  /** @param addressBits 地址宽度（位），1 到 16。 */
  explicit BusMap(unsigned addressBits);

  /** 挂载地址范围，规则见 contracts::hardware::Bus::mount。 */
  void mount(std::uint16_t first, std::uint16_t last, contracts::hardware::BusDevice& device);

  /** 读，规则见 contracts::hardware::Bus::read。 */
  std::uint8_t read(std::uint16_t address);

  /** 写，规则见 contracts::hardware::Bus::write。 */
  void write(std::uint16_t address, std::uint8_t value);

  /** 调试用的读：经设备的 peek，不改变总线上的值，没有设备或设备不驱动的位读出 0。 */
  [[nodiscard]] std::uint8_t peek(std::uint16_t address) const;

  /** 安装或去掉访问探针（空指针表示去掉）；探针在每次读写之后被通知，必须活到被去掉或总线销毁。 */
  void setProbe(contracts::hardware::AccessProbe* probe) { probe_ = probe; }

 private:
  struct Mapping {
    std::uint32_t first;
    std::uint32_t last;
    contracts::hardware::BusDevice* device;
  };

  /** 找到覆盖地址的挂载，没有返回空指针。 */
  [[nodiscard]] const Mapping* find(std::uint32_t address) const;

  std::uint32_t addressMask_;
  /** 按起点升序，互不重叠。 */
  std::vector<Mapping> mappings_;
  /** 总线上最近一次的值（读或写）。 */
  std::uint8_t latch_{0};
  contracts::hardware::AccessProbe* probe_{nullptr};
};

}  // namespace ember::logic::hardware
