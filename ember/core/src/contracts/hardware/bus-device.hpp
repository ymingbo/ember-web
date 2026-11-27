/*
 * ------------------------------------------------------------------------
 * 名称：bus-device.hpp
 * 说明：硬件契约：总线设备。响应总线上一个地址范围的读写，读的结果带“驱动了哪些数据位”。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * 备注：没有被驱动的数据位保持总线上最近一次的值（开放总线），这个值由总线保存，设备不用猜。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <cstdint>

namespace ember::contracts::hardware {

/** 一次读的结果。 */
struct BusRead {
  /** 设备给出的数据；没有被驱动的位无意义。 */
  std::uint8_t value{0};
  /** 设备驱动的数据位掩码：1 表示该位由设备驱动，0 表示该位无人驱动、读到总线上最近的值。 */
  std::uint8_t drivenMask{0xFF};
};

/** 八个数据位都由设备驱动的读结果。 */
[[nodiscard]] constexpr BusRead driveBus(std::uint8_t value) noexcept { return BusRead{value, 0xFF}; }

/** 只驱动部分数据位的读结果。 */
[[nodiscard]] constexpr BusRead driveBus(std::uint8_t value, std::uint8_t drivenMask) noexcept { return BusRead{value, drivenMask}; }

/** 完全不驱动数据位的读结果。 */
[[nodiscard]] constexpr BusRead releaseBus() noexcept { return BusRead{0, 0}; }

/** 总线设备：挂在总线某个地址范围上的芯片、存储或寄存器组。 */
class BusDevice {
 public:
  virtual ~BusDevice() = default;

  /**
   * 读。可以有副作用（如读状态寄存器会清标志）。
   * @param address 总线上的绝对地址（不是范围内的偏移），镜像由设备自己处理。
   */
  virtual BusRead read(std::uint16_t address) = 0;

  /**
   * 写。
   * @param address 总线上的绝对地址。
   * @param value 写入的数据。
   */
  virtual void write(std::uint16_t address, std::uint8_t value) = 0;

  /**
   * 调试用的读：不改变任何状态、不回调脚本。默认不驱动数据线（寄存器类设备读不到内容）；
   * 存储类设备覆盖它，返回与 read 相同的数据。
   * @param address 总线上的绝对地址。
   */
  virtual BusRead peek(std::uint16_t address) {
    static_cast<void>(address);
    return releaseBus();
  }
};

}  // namespace ember::contracts::hardware
