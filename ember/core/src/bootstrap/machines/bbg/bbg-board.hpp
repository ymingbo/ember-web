/*
 * ------------------------------------------------------------------------
 * 名称：bbg-board.hpp
 * 说明：主机型号 bbg（步步高学习机）主板上的存储与整机寄存器：BIOS ROM、DRAM、字符 RAM、名称表显存，以及 $FF00-$FFFF 的整机寄存器、扫描线计数与分屏、MMC3 模式。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-16
 * 备注：这些是主机型号自己的胶合逻辑（不属于任何芯片模块），只由 bbg.cpp 创建并挂到总线上；容量与寄存器的行为按原厂 BIOS 的用法与它在模拟器里启动的结果定，没有经过真机核对；
 *       CPU 地址 $4020-$FFFF：
 *         $4020-$43FF 没有器件（MMC3 模式下与 $4400 以上一样是 DRAM）；$4400-$7FFF 是 DRAM 顶部 16K（系统工作区，$4000-$7FFF 整段对应它，$4000-$43FF 被寄存器区占去）；
 *         $8000-$BFFF：两个 8K 窗口，$FF04 选 16K 页（位 7 为 1 选 DRAM，否则选 ROM），$FF14、$FF1C 分别选 8K 页并同时选 DRAM；
 *         $C000-$FFFF：$FF01 位 3 或 $FF11 位 7 为 1 时是 DRAM（8K 页由 $FF24、$FF2C 选），否则是 ROM 的最后 16K；
 *         $FF00-$FFFF 的写入总是进整机寄存器；读时只有 8 的倍数的地址（且 $C000-$FFFF 不是 DRAM 时）读寄存器，其余读 ROM；
 *       整机寄存器：$FF00 键盘灯（忽略）、$FF01 视频控制（位 1-0 名称表连接，位 2 允许扫描线 IRQ，位 3 $C000 起用 DRAM，位 5 MMC3 模式，位 6 分屏）、
 *         $FF02 与 $FF06 扫描线计数器的低、高 4 位、$FF03/$FF0B/$FF13/$FF1B 各选一个 2K 字符页、$FF23 起每 8 个地址一个 1K 字符页、$FF04/$FF14/$FF1C 程序页、$FF24/$FF2C DRAM 页、
 *         $FF0A、$FF1A、$FF12、$FF22、$FF2A 分屏队列、$FF10 语音复位（位 0 上升沿）与 $FF18 语音数据、$FF11 与 $FF19 强制 DRAM、$FF3C 游戏数据大小、
 *         $FF40-$FF50 并口、$FF80-$FFB8 软驱（$FF80/$FF88 DMA 数据、$FF90 控制与数据请求、$FF98 中断位、$FFA0 复位与主状态、$FFA8 数据、$FFB8 盘片更换与数据率）、$FFB0 中断确认；
 *       $2000、$2001、$2005 的写入在 DRAM 映射到 $C000 以上时同时写入系统工作区的 $6000 起的对应地址（固件靠它读回这几个只写寄存器）；
 *       IRQ：计数器每过一行加 1，从 $FF 回到 0 时（且允许、显示开启、在可见行内）发出 IRQ；分屏模式下每行把计数器与分屏队列比较，到点时换字符页，第 239 行发出 IRQ；MMC3 模式下 IRQ 由 MMC3 芯片按 A12 计数；
 *       MMC3 模式：$8000-$FFFF 的写入交给 MMC3 芯片，程序页与图案页按芯片的页号换算到 DRAM 与字符 RAM；
 *       行时钟：每个 PPU 行的第 260 点（背景取图结束、下一行预取开始之前）触发一次，行号与 PPU 的行号一致。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "contracts/hardware/bus-device.hpp"
#include "contracts/hardware/irq-line.hpp"
#include "contracts/hardware/mapper-circuit.hpp"
#include "contracts/hardware/parallel-port.hpp"
#include "contracts/hardware/scheduler.hpp"
#include "bootstrap/machines/common/fdc-port-bridge.hpp"
#include "logic/hardware/fdc-upd765/fdc-upd765.hpp"
#include "logic/hardware/ppu-2c02/ppu-2c02.hpp"
#include "logic/hardware/speech-lpc/speech-lpc.hpp"

namespace ember::bootstrap {

/** 步步高机型的容量差别。 */
struct BbgVariant {
  /** 机型标识。 */
  const char* id;
  /** BIOS ROM 的容量（字节），必须是 8K 的 2 的幂倍。 */
  std::size_t romSize;
  /** DRAM 的容量（字节），必须是 8K 的 2 的幂倍；顶部 32K 用作系统工作区与 MMC3 模式的固定页。 */
  std::size_t dramSize;
  /** 普通模式下字符页（1K）的页号掩码，字符 RAM 的容量是 (掩码 + 1) 个 1K。 */
  unsigned characterPageMask;

  /** 步步高软驱一号：128K BIOS、512K DRAM、32K 字符页。 */
  static const BbgVariant& floppyOne();
  /** 步步高 98：2048K BIOS、2048K DRAM、256K 字符页。 */
  static const BbgVariant& model98();
};

/** 步步高主板。 */
class BbgBoard final : public contracts::hardware::EventListener {
 public:
  /** 字符 RAM 的物理容量（字节）。 */
  static constexpr std::size_t CHARACTER_RAM_SIZE = 256 * 1024;
  /** 名称表显存的容量（字节）。 */
  static constexpr std::size_t NAMETABLE_SIZE = 2048;
  /** 系统工作区的容量（字节）：DRAM 顶部 32K 里的前 16K。 */
  static constexpr std::size_t SYSTEM_RAM_SIZE = 16 * 1024;
  /** 行时钟在一行里的第几点触发。 */
  static constexpr int LINE_EVENT_DOT = 260;

  /** 寄存器的快照，供调试。 */
  struct Registers {
    std::uint8_t videoControl;
    std::uint8_t prgBank0;
    std::uint8_t prgBank1;
    bool ramEnable;
    std::uint8_t cdBank;
    std::uint8_t efBank;
    bool forceRam;
    std::uint8_t scanlineCounter;
    bool mmc3Mode;
    bool splitMode;
  };

  /** CPU 地址对应的存储位置。 */
  struct CpuLocation {
    enum class Kind : std::uint8_t { None, Register, Rom, Dram };
    Kind kind{Kind::None};
    std::size_t offset{0};
  };

  /**
   * @param variant 机型的容量。
   * @param rom BIOS ROM 的内容，大小必须等于 variant.romSize。
   * @param scheduler 调度器：行时钟靠它的事件推进。
   * @param irq IRQ 线，本板在其上登记一个来源。
   * @param ppu PPU：读它的渲染开关。
   * @param ppuRegisters $2000-$3FFF 的 PPU 寄存器设备，本板代理它的写入以做系统工作区的影子写。
   * @param fdc 软驱控制器。
   * @param parallel 并口。
   * @param speech 语音芯片。
   * @param mmc3 MMC3 电路。
   * @param lineTicks 一个 PPU 行占的主时钟周期数。
   * @param dotTicks 一个 PPU 点占的主时钟周期数。
   * @throws shared::CoreError hardware.invalidArgument：容量不合格。
   */
  BbgBoard(const BbgVariant& variant, std::vector<std::uint8_t> rom, contracts::hardware::Scheduler& scheduler, contracts::hardware::IrqLine& irq, const logic::hardware::Ppu2c02& ppu,
           contracts::hardware::BusDevice& ppuRegisters, logic::hardware::FdcUpd765& fdc, contracts::hardware::ParallelPort& parallel, logic::hardware::SpeechLpc& speech,
           contracts::hardware::MapperCircuit& mmc3, contracts::hardware::MasterTime lineTicks, contracts::hardware::MasterTime dotTicks);

  BbgBoard(const BbgBoard&) = delete;
  BbgBoard& operator=(const BbgBoard&) = delete;

  /** CPU 总线上 $4020-$FFFF 的设备。 */
  contracts::hardware::BusDevice& cpuSide() { return cpuSide_; }
  /** PPU 总线上 $0000-$3EFF 的设备。 */
  contracts::hardware::BusDevice& videoSide() { return videoSide_; }
  /** CPU 总线上 $2000-$3FFF 的设备（代理 PPU 寄存器）。 */
  contracts::hardware::BusDevice& ppuPort() { return ppuPort_; }

  /**
   * 复位：寄存器回到初值；上电时存储清零，软复位时存储保持。
   * @param power 是否上电。
   */
  void reset(bool power);

  /** 从现在起按 PPU 的行节拍启动行时钟：现在是第 0 行的第 0 点。 */
  void startLineClock();
  /** 停止行时钟。 */
  void stopLineClock();

  void onEvent(contracts::hardware::MasterTime when, std::uint32_t tag) override;

  // 调试与测试用
  [[nodiscard]] const BbgVariant& variant() const { return variant_; }
  [[nodiscard]] const std::vector<std::uint8_t>& rom() const { return rom_; }
  [[nodiscard]] const std::vector<std::uint8_t>& dram() const { return dram_; }
  [[nodiscard]] std::vector<std::uint8_t>& dram() { return dram_; }
  [[nodiscard]] const std::vector<std::uint8_t>& characterRam() const { return characterRam_; }
  [[nodiscard]] std::vector<std::uint8_t>& characterRam() { return characterRam_; }
  [[nodiscard]] const std::array<std::uint8_t, NAMETABLE_SIZE>& nametable() const { return nametable_; }
  [[nodiscard]] std::array<std::uint8_t, NAMETABLE_SIZE>& nametable() { return nametable_; }
  [[nodiscard]] Registers registers() const;
  /** 系统工作区（$4000-$7FFF）在 DRAM 里的起点。 */
  [[nodiscard]] std::size_t systemBase() const { return systemBase_; }
  /** CPU 地址当前对应的存储位置。 */
  [[nodiscard]] CpuLocation locate(std::uint16_t address) const;
  /** PPU 地址（$0000-$3EFF）当前对应的位置：图案区给字符 RAM 的偏移，名称表给显存偏移。 */
  [[nodiscard]] std::size_t locateVideo(std::uint16_t address, bool& isPattern) const;
  /** 当前行号（行时钟下一次触发的行）。 */
  [[nodiscard]] int line() const { return line_; }

 private:
  /** CPU 侧：$4020-$FFFF。 */
  class CpuSide final : public contracts::hardware::BusDevice {
   public:
    explicit CpuSide(BbgBoard& board) : board_(board) {}
    contracts::hardware::BusRead read(std::uint16_t address) override;
    contracts::hardware::BusRead peek(std::uint16_t address) override;
    void write(std::uint16_t address, std::uint8_t value) override;

   private:
    BbgBoard& board_;
  };

  /** PPU 侧：图案区与名称表。 */
  class VideoSide final : public contracts::hardware::BusDevice {
   public:
    explicit VideoSide(BbgBoard& board) : board_(board) {}
    contracts::hardware::BusRead read(std::uint16_t address) override;
    contracts::hardware::BusRead peek(std::uint16_t address) override;
    void write(std::uint16_t address, std::uint8_t value) override;

   private:
    BbgBoard& board_;
  };

  /** $2000-$3FFF：把读写交给 PPU，写入再做影子写。 */
  class PpuPort final : public contracts::hardware::BusDevice {
   public:
    explicit PpuPort(BbgBoard& board) : board_(board) {}
    contracts::hardware::BusRead read(std::uint16_t address) override;
    contracts::hardware::BusRead peek(std::uint16_t address) override;
    void write(std::uint16_t address, std::uint8_t value) override;

   private:
    BbgBoard& board_;
  };

  /** DRAM 里 8K 页的字节（页号按 DRAM 容量取低位）。 */
  [[nodiscard]] std::size_t dramOffset(unsigned page, std::uint16_t address) const;
  /** MMC3 模式下 $8000-$FFFF 的地址对应的 DRAM 偏移。 */
  [[nodiscard]] std::size_t mmc3DramOffset(std::uint16_t address) const;
  /** $C000-$FFFF 是否用 DRAM。 */
  [[nodiscard]] bool upperIsRam() const { return mapRam_ || forceRam_; }

  std::uint8_t readCpu(std::uint16_t address, bool sideEffects, std::uint8_t& drivenMask);
  void writeCpu(std::uint16_t address, std::uint8_t value);
  /** 整机寄存器的读：8 的倍数的地址；没有这个寄存器时为空。 */
  std::optional<std::uint8_t> readRegister(std::uint16_t address, bool sideEffects);
  void writeRegister(std::uint16_t address, std::uint8_t value);
  void writeProgramRam(std::uint16_t address, std::uint8_t value);
  void shadowPpuWrite(std::uint16_t address, std::uint8_t value);
  /** 普通模式下由字符页寄存器重新算出生效的字符页。 */
  void applyCharacterBanks();
  void raiseIrq() { irq_.set(irqSource_, true); }
  void clearIrq() { irq_.set(irqSource_, false); }
  /** 一行的行时钟处理。 */
  void onLine(int line);

  std::uint8_t readVideo(std::uint16_t address, bool notifyCircuit);
  void writeVideo(std::uint16_t address, std::uint8_t value);
  [[nodiscard]] std::size_t characterOffset(std::uint16_t address) const;
  [[nodiscard]] std::size_t nametableOffset(std::uint16_t address) const;

  BbgVariant variant_;
  contracts::hardware::Scheduler& scheduler_;
  contracts::hardware::IrqLine& irq_;
  contracts::hardware::SignalSource irqSource_;
  const logic::hardware::Ppu2c02& ppu_;
  contracts::hardware::BusDevice& ppuRegisters_;
  FdcPortBridge floppy_;
  contracts::hardware::ParallelPort& parallel_;
  logic::hardware::SpeechLpc& speech_;
  contracts::hardware::MapperCircuit& mmc3_;
  contracts::hardware::MasterTime lineTicks_;
  contracts::hardware::MasterTime dotTicks_;

  CpuSide cpuSide_{*this};
  VideoSide videoSide_{*this};
  PpuPort ppuPort_{*this};

  std::vector<std::uint8_t> rom_;
  std::vector<std::uint8_t> dram_;
  std::vector<std::uint8_t> characterRam_;
  std::array<std::uint8_t, NAMETABLE_SIZE> nametable_{};
  unsigned romPageMask_;
  unsigned dramPageMask_;
  std::size_t systemBase_;

  // 整机寄存器
  std::uint8_t videoControl_{0};
  bool irqEnable_{false};
  bool mapRam_{false};
  bool mmc3Mode_{false};
  bool splitMode_{false};
  std::array<std::uint8_t, 2> prgBank_{};
  bool ramEnable_{false};
  std::uint8_t cdBank_{0};
  std::uint8_t efBank_{0};
  bool forceRam_{false};
  std::array<std::uint8_t, 8> characterReg_{};
  /** 生效的字符页：普通模式下由 characterReg_ 得出，分屏模式下前 4 页由分屏队列改写。 */
  std::array<unsigned, 8> characterEffective_{};
  std::uint8_t scanlineCounter_{0};
  std::uint8_t gameDataSize_{0};
  bool speechResetBit_{false};
  std::vector<std::uint8_t> splitLines_;
  std::vector<std::uint8_t> splitBanks_;
  std::vector<std::uint8_t> pendingLines_;
  std::vector<std::uint8_t> pendingBanks_;
  std::size_t splitIndex_{0};
  // MMC3 模式
  unsigned mmc3ProgramMask_{0x3F};
  unsigned mmc3CharacterMask_{0x7F};

  // 行时钟
  contracts::hardware::EventId lineEvent_{contracts::hardware::NO_EVENT};
  contracts::hardware::MasterTime nextLineTime_{0};
  int line_{0};
};

}  // namespace ember::bootstrap
