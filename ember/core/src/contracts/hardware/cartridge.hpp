/*
 * ------------------------------------------------------------------------
 * 名称：cartridge.hpp
 * 说明：硬件契约：卡带的对外接口。CPU 侧与 PPU 侧各是一个总线设备，另有显存控制线。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * 备注：卡带由存储芯片与 mapper 芯片组成，决定访问由哪块存储响应；引脚细节随主机型号对照真机资料逐项定。
 *       卡带向 CPU 的 IRQ 输出经构造时接入的 IrqLine 发出，扩展音频以后随声音阶段加入。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <cstdint>

#include "contracts/hardware/bus-device.hpp"
#include "contracts/hardware/input-event.hpp"

namespace ember::contracts::hardware {

/** 显存控制线：PPU 访问名称表时，决定由主板显存还是卡带响应。 */
struct CiramControl {
  /** 主板显存（CIRAM）的片选：true 表示由主板显存响应，false 表示由卡带自带的显存响应。 */
  bool enable{true};
  /** 主板显存的 A10：选择两个 1K 页中的哪一个。 */
  bool a10{false};
};

/** 卡带。 */
class Cartridge {
 public:
  virtual ~Cartridge() = default;

  /** CPU 侧：响应 CPU 总线上属于卡带的地址。 */
  virtual BusDevice& cpuSide() = 0;

  /** PPU 侧：响应 PPU 总线上的图案表地址（以及卡带自带显存响应的名称表地址）。 */
  virtual BusDevice& ppuSide() = 0;

  /**
   * 显存控制线：PPU 访问名称表地址时读取。
   * @param ppuAddress PPU 总线地址（$2000-$3EFF）。
   */
  [[nodiscard]] virtual CiramControl ciramControl(std::uint16_t ppuAddress) const = 0;

  /**
   * 复位：mapper 芯片按真机的行为保持或清除状态。
   * @param power 为 true 表示上电复位。
   */
  virtual void reset(bool power) = 0;

  /** 用户输入：键盘、鼠标、游戏手柄的原始事件，卡带按自己的程序决定怎么用；不用输入的卡带忽略。 */
  virtual void input(const InputEvent& event) = 0;

  /** 卡带是否要观察 PPU 地址线（如 MMC3 数 A12 的上升沿）；不观察的卡带返回 false，卡槽就不会逐次通知。 */
  [[nodiscard]] virtual bool watchesVideoAddress() const { return false; }

  /**
   * PPU 对显存总线的一次访问（图案表与名称表的每次读写）；只有 watchesVideoAddress 为真时才会被调用。
   * @param ppuAddress PPU 总线地址（0 到 $3EFF）。
   */
  virtual void onVideoAddress(std::uint16_t ppuAddress) { static_cast<void>(ppuAddress); }
};

}  // namespace ember::contracts::hardware
