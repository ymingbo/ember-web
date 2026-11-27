/*
 * ------------------------------------------------------------------------
 * 名称：machine-factory.hpp
 * 说明：runtime 契约：按主机型号名创建一台还没有卡带的主机。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * 备注：实现在 bootstrap/machines，只有组装根认识具体的主机型号；mapper 清单里的 host 名与这里的型号名一致。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "contracts/hardware/machine.hpp"

namespace ember::contracts::runtime {

/** 主机型号目录。 */
class MachineFactory {
 public:
  virtual ~MachineFactory() = default;

  /**
   * 创建主机：已装配好芯片与总线，没有通电，卡槽是空的。
   * @param host 主机型号名，如 fc。
   * @throws shared::CoreError runtime.hostUnsupported：程序里没有这个型号。
   */
  [[nodiscard]] virtual std::unique_ptr<hardware::Machine> create(const std::string& host) const = 0;

  /**
   * 创建整机机型的主机：ROM 在主板上，没有卡带，没有通电。
   * @param machine 机型标识，如 sb2000。
   * @param rom ROM ROM 的内容（不含文件头）。
   * @throws shared::CoreError runtime.hostUnsupported：程序里没有这个机型；hardware.invalidArgument：ROM 的大小不对。
   */
  [[nodiscard]] virtual std::unique_ptr<hardware::Machine> createWithRom(const std::string& machine, std::vector<std::uint8_t> rom) const = 0;
};

}  // namespace ember::contracts::runtime
