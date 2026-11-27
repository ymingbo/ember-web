/*
 * ------------------------------------------------------------------------
 * 名称：fdc-port-bridge.hpp
 * 说明：软驱控制器的端口转接：把各机型地址译码出来的端口号换成对软驱控制器模块的读写；小霸王 2000、步步高、小霸王 97、裕兴四种学习机共用。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-21
 * 备注：控制器模块与总线地址无关，每个机型的地址表不同，所以地址译码留在机型自己的主板里，这里只做“端口号到控制器调用”的换算，提供两种端口表：
 *       一是寄存器号表（readRegister、writeRegister）：端口号就是控制器的寄存器号（0 状态 A、2 DOR、3 中断位、4 主状态、5 数据、7 盘片更换位与数据率）；
 *       二是 PC 式端口表（readPcPort、writePcPort），沿用标准 PC 软驱控制器的 8 个端口：0 与 1 是 DMA 数据口（1 同时给出终止计数），2 读出位 6 是数据请求、写 DOR，3 读出位 6 是中断标志，
 *       4 读主状态、写位 6 是控制器复位脚，5 读写数据，7 读盘片更换位、写数据率；端口 6 与 3 的写入由主板自己处理。
 *       调试读（sideEffects 为假）不改变控制器的状态：DMA 数据口与盘片更换位读不到内容。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <cstdint>
#include <optional>

#include "logic/hardware/fdc-upd765/fdc-upd765.hpp"

namespace ember::bootstrap {

/** 软驱控制器的端口转接。 */
class FdcPortBridge final {
 public:
  explicit FdcPortBridge(logic::hardware::FdcUpd765& fdc) : fdc_(fdc) {}

  /** 中断标志（状态寄存器 A 位 7）。 */
  [[nodiscard]] bool irqRaised() const { return fdc_.irqRaised(); }

  /** 按控制器的寄存器号读；调试读（sideEffects 为假）不改变状态。 */
  std::uint8_t readRegister(unsigned port, bool sideEffects) { return sideEffects ? fdc_.readRegister(port) : fdc_.peekRegister(port); }

  /** 按控制器的寄存器号写。 */
  void writeRegister(unsigned port, std::uint8_t value) { fdc_.writeRegister(port, value); }

  /** PC 式端口表读；返回空表示这个端口读不到内容（调试读 DMA 数据口与盘片更换位，以及没有定义的端口 6）。 */
  std::optional<std::uint8_t> readPcPort(unsigned port, bool sideEffects) {
    switch (port) {
      case 0:
        if (!sideEffects) return std::nullopt;
        return fdc_.receiveFromDma();
      case 1:
        if (!sideEffects) return std::nullopt;
        fdc_.setTerminalCount();
        return fdc_.receiveFromDma();
      case 2:
        return static_cast<std::uint8_t>(fdc_.dmaRequest() ? 0x40 : 0x00);
      case 3:
        if (!sideEffects) return static_cast<std::uint8_t>(fdc_.irqRaised() ? 0x40 : 0x00);
        return fdc_.readRegister(3);
      case 4:
      case 5:
        return readRegister(port, sideEffects);
      case 7:
        if (!sideEffects) return std::nullopt;
        return fdc_.readRegister(7);
      default:
        return std::nullopt;
    }
  }

  /** PC 式端口表写；端口 3 与 6 不处理。 */
  void writePcPort(unsigned port, std::uint8_t value) {
    switch (port) {
      case 0:
        fdc_.sendToDma(value);
        break;
      case 1:
        fdc_.setTerminalCount();
        fdc_.sendToDma(value);
        break;
      case 4:
        fdc_.setResetPin((value & 0x40U) != 0);
        break;
      case 2:
      case 5:
      case 7:
        fdc_.writeRegister(port, value);
        break;
      default:
        break;
    }
  }

 private:
  logic::hardware::FdcUpd765& fdc_;
};

}  // namespace ember::bootstrap
