/*
 * ------------------------------------------------------------------------
 * 名称：machine.hpp
 * 说明：硬件契约：一台装配好的主机。电源、复位、运行。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * 备注：主机由 bootstrap/machines 按主机型号装配；状态快照在调试服务阶段补充。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <cstdint>
#include <vector>

#include "contracts/hardware/button-input.hpp"
#include "contracts/hardware/cartridge-slot.hpp"
#include "contracts/hardware/clock.hpp"
#include "contracts/hardware/input-event.hpp"
#include "contracts/hardware/cpu.hpp"
#include "contracts/hardware/floppy-drive.hpp"
#include "contracts/hardware/irq-output.hpp"
#include "contracts/hardware/machine-debugger.hpp"
#include "contracts/hardware/ppu.hpp"
#include "contracts/hardware/printer-output.hpp"
#include "contracts/hardware/tape-deck.hpp"

namespace ember::contracts::hardware {

/** 主机；手柄口的集合（PadSink）由主机提供，供卡带的脚本设置按键。 */
class Machine : public PadSink {
 public:
  virtual ~Machine() = default;

  /** 上电：创建或重置全部芯片与设备到上电状态，随后可以运行。 */
  virtual void powerOn() = 0;

  /** 断电：停止运行，芯片状态丢弃；卡带与介质保持不变。 */
  virtual void powerOff() = 0;

  /** 软复位：只发复位信号，各芯片按自己的复位行为处理。断电时调用无效果。 */
  virtual void softReset() = 0;

  /** 是否已上电。 */
  [[nodiscard]] virtual bool isPoweredOn() const = 0;

  /**
   * 运行指定的主时钟周期数。CPU 的指令不会被中途打断，所以实际经过的时间可能略多。
   * 断电时调用无效果。
   * @param ticks 要运行的主时钟周期数。
   */
  virtual void run(MasterTime ticks) = 0;

  /** 运行到 PPU 完成下一帧画面，CPU 的指令不会被中途打断。断电时调用无效果。 */
  virtual void runFrame() = 0;

  /** 最近一次完成的画面；没有完成过画面时宽高为 0。 */
  [[nodiscard]] virtual FrameBuffer frame() const = 0;

  /** 画面的帧率（每秒帧数），由主时钟与每帧的时间算出；运行服务按它限速，使声音的产出与播放速度一致。 */
  [[nodiscard]] virtual double framesPerSecond() const = 0;

  /** 声音的采样率（赫兹），单声道。 */
  [[nodiscard]] virtual unsigned audioSampleRate() const = 0;

  /**
   * 取走自上次以来生成的声音采样（单声道，范围 -1 到 1）；先清空 samples 再放入。
   * 采样追赶到主机当前的时间；断电时没有新采样。没有人取走时主机只保留最近一小段。
   */
  virtual void takeAudio(std::vector<float>& samples) = 0;

  /**
   * 用户输入：键盘与鼠标的原始事件，由主机型号自己（或经典 FC 的卡带脚本）处理；游戏手柄的事件不会交给它，手柄口只经 setButtons 设置。
   * @param event 输入事件。
   */
  virtual void input(const InputEvent& event) = 0;

  /**
   * 键盘现在是否属于机器：true 时键盘按键全部交给 input，不能当手柄用；false 时没有被机器占用的键盘按键可以按手柄绑定变成手柄口的按键。
   * 经典 FC 没有键盘，总是 false；学习机在 DOS 里为 true，进入 FC 兼容模式后为 false。
   */
  [[nodiscard]] virtual bool keyboardIsMachineInput() const { return false; }

  /**
   * 主机自己现在是否带鼠标：学习机在 6576 模式下有串口鼠标，进入 FC 兼容模式后没有；经典 FC 没有（卡带的 mapper 可以另外声明鼠标，由运行服务合并判断）。
   */
  [[nodiscard]] virtual bool hasMouse() const { return false; }

  /** CPU 当前的寄存器，供测试与调试读取；只在指令之间读取有意义。 */
  [[nodiscard]] virtual CpuRegisters cpuRegisters() const = 0;

  /** 主时钟。 */
  [[nodiscard]] virtual const Clock& clock() const = 0;

  /** 卡槽。 */
  virtual CartridgeSlot& cartridgeSlot() = 0;

  /** 卡槽的 IRQ 引脚：卡带经它向 CPU 发出中断；创建卡带时接入。 */
  virtual IrqOutput& cartridgeIrq() = 0;

  /** 调试接口；主机型号不支持调试时为空指针。只在模拟线程使用。 */
  virtual MachineDebugger* debugger() { return nullptr; }

  /** 软驱驱动器的个数；没有软驱的主机型号为 0。 */
  [[nodiscard]] virtual unsigned floppyDriveCount() const { return 0; }

  /**
   * 取软驱驱动器；编号超出 floppyDriveCount() 时返回空指针。盘片与电源无关：断电时盘片保持不变。只在模拟线程使用。
   */
  virtual FloppyDrive* floppyDrive(unsigned index) {
    static_cast<void>(index);
    return nullptr;
  }

  /** 并口打印机的控制；主机型号没有打印机时为空指针。只在模拟线程使用。 */
  virtual PrinterControl* printer() { return nullptr; }

  /** 主机上的磁带接口所接的磁带机；主机型号没有磁带接口时为空指针。磁带机是独立于主机的设备，主机只在通电时接上它：断电、换卡带都会让它停止，磁带留在里面。只在模拟线程使用。 */
  virtual TapeControl* tape() { return nullptr; }

  /**
   * 接上或拔下 Family BASIC 键盘（经典 FC 扩展口上的外设）；接上后键盘按键按它的键位送给机器，keyboardIsMachineInput 为 true。
   * 主机型号没有扩展口时没有效果。只在模拟线程使用。
   */
  virtual void connectFamilyKeyboard(bool connected) { static_cast<void>(connected); }
};

}  // namespace ember::contracts::hardware
