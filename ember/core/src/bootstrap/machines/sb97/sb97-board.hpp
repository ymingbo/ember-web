/*
 * ------------------------------------------------------------------------
 * 名称：sb97-board.hpp
 * 说明：主机型号 sb97（小霸王 97 学习机）主板上的存储与整机寄存器：1MB 的 BIOS ROM、128K 的程序 RAM、8K 的工作 RAM、8K 的字符 RAM、2K 的名称表显存，以及 $5000 起的整机寄存器。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-17
 * 备注：这些是主机型号自己的胶合逻辑（不属于任何芯片模块），只由 sb97.cpp 创建并挂到总线上；
 *       CPU 地址 $4020-$FFFF：
 *         $4020-$40FF 读出 0、写入忽略；$4100-$4FFF 没有器件；
 *         $5000 程序页（写）：位 7 为 0 选 ROM，为 1 选 128K 的程序 RAM；
 *         $5200 整机控制（写）：位 0 名称表镜像（1 水平、0 垂直），位 2 为 1 是 32K 模式，位 0、1 等于 2 时背景图案表按名称表序号选（见 backgroundUsesTableOne）；
 *         $5300 语音芯片：读状态、写码流字节；$5500-$5507 软驱控制器写（$5501 对应寄存器 2，$5500 对应寄存器 7，其余取低 3 位），$5600-$5607 读；其余 $5000-$5FFF 没有器件；
 *         $6000-$7FFF：8K 工作 RAM；
 *         $8000-$FFFF 程序窗口：
 *           16K 模式：$8000-$BFFF 是第 $5000 页（按 64 页取模；RAM 模式下是程序 RAM 的第 $5000&7 个 16K），$C000-$FFFF 固定是 ROM 第 0 页；
 *           32K 模式：$8000-$FFFF 是 ROM 的第 $5000 个 32K（按 32 个取模；RAM 模式下是程序 RAM 的第 $5000&3 个 32K，可写）；
 *           ROM 与第 0 页的写入忽略；
 *       PPU 地址：$0000-$1FFF 是字符 RAM，$2000-$2FFF 是名称表显存（2K，按 $5200 位 0 镜像）；
 *       软驱控制器的中断标志直接接到 CPU 的 IRQ 线：每次访问软驱寄存器之后与控制器请求中断时都按标志刷新这条线；
 *       上电后寄存器为 0，存储清零。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "contracts/hardware/bus-device.hpp"
#include "contracts/hardware/interrupt-sink.hpp"
#include "contracts/hardware/irq-line.hpp"
#include "bootstrap/machines/common/fdc-port-bridge.hpp"
#include "logic/hardware/fdc-upd765/fdc-upd765.hpp"
#include "logic/hardware/speech-lpc/speech-lpc.hpp"

namespace ember::bootstrap {

/** 小霸王 97 主板。 */
class Sb97Board final {
 public:
  /** 一页 ROM 的大小（16K）。 */
  static constexpr std::size_t ROM_PAGE_SIZE = 16 * 1024;
  /** ROM 的页数。 */
  static constexpr std::size_t PAGE_COUNT = 64;
  /** BIOS ROM 的容量（字节）。 */
  static constexpr std::size_t ROM_SIZE = ROM_PAGE_SIZE * PAGE_COUNT;
  /** 程序 RAM 的容量（字节）。 */
  static constexpr std::size_t PROGRAM_RAM_SIZE = 128 * 1024;
  /** 工作 RAM 的容量（字节），在 $6000-$7FFF。 */
  static constexpr std::size_t WORK_RAM_SIZE = 8 * 1024;
  /** 字符 RAM 的容量（字节）。 */
  static constexpr std::size_t CHARACTER_RAM_SIZE = 8 * 1024;
  /** 名称表显存的容量（字节）。 */
  static constexpr std::size_t NAMETABLE_SIZE = 2048;

  /** 寄存器的快照，供调试。 */
  struct Registers {
    std::uint8_t programPage;
    std::uint8_t control;
    bool programRamMode;
    bool thirtyTwoKilobyteMode;
    bool horizontalMirroring;
    bool tableSwitch;
  };

  /** CPU 地址对应的存储位置。 */
  struct CpuLocation {
    enum class Kind : std::uint8_t { None, Register, Rom, ProgramRam, WorkRam };
    Kind kind{Kind::None};
    std::size_t offset{0};
  };

  /**
   * @param rom BIOS ROM 的内容，大小必须等于 ROM_SIZE。
   * @param irq IRQ 线，本板在其上登记一个来源（软驱的中断）。
   * @param fdc 软驱控制器。
   * @param speech 语音芯片。
   * @throws shared::CoreError hardware.invalidArgument：ROM 大小不对。
   */
  Sb97Board(std::vector<std::uint8_t> rom, contracts::hardware::IrqLine& irq, logic::hardware::FdcUpd765& fdc, logic::hardware::SpeechLpc& speech);

  Sb97Board(const Sb97Board&) = delete;
  Sb97Board& operator=(const Sb97Board&) = delete;

  /** CPU 总线上 $4020-$FFFF 的设备。 */
  contracts::hardware::BusDevice& cpuSide() { return cpuSide_; }
  /** PPU 总线上 $0000-$3EFF 的设备。 */
  contracts::hardware::BusDevice& videoSide() { return videoSide_; }

  /**
   * 复位：寄存器回到 0；上电时存储清零，软复位时存储保持。
   * @param power 是否上电。
   */
  void reset(bool power);

  /** 软驱控制器的中断标志变化后调用：IRQ 线跟着标志。 */
  void refreshFdcInterrupt();

  /**
   * 背景图块是否用图案表 1（交给 PPU 的背景图案表选择）：名称表 2 的图块总是用；名称表 1、3 的图块在 $5200 位 0、1 等于 2 时用。
   * @param nametable 取图块时的名称表序号（0 到 3，镜像之前的逻辑序号）。
   */
  [[nodiscard]] bool backgroundUsesTableOne(unsigned nametable) const;

  // 调试与测试用
  [[nodiscard]] const std::vector<std::uint8_t>& rom() const { return rom_; }
  [[nodiscard]] const std::vector<std::uint8_t>& programRam() const { return programRam_; }
  [[nodiscard]] std::vector<std::uint8_t>& programRam() { return programRam_; }
  [[nodiscard]] const std::vector<std::uint8_t>& workRam() const { return workRam_; }
  [[nodiscard]] std::vector<std::uint8_t>& workRam() { return workRam_; }
  [[nodiscard]] const std::vector<std::uint8_t>& characterRam() const { return characterRam_; }
  [[nodiscard]] std::vector<std::uint8_t>& characterRam() { return characterRam_; }
  [[nodiscard]] const std::array<std::uint8_t, NAMETABLE_SIZE>& nametable() const { return nametable_; }
  [[nodiscard]] std::array<std::uint8_t, NAMETABLE_SIZE>& nametable() { return nametable_; }
  [[nodiscard]] Registers registers() const;
  /** CPU 地址当前对应的存储位置。 */
  [[nodiscard]] CpuLocation locate(std::uint16_t address) const;
  /** PPU 地址（$0000-$3EFF）当前对应的位置：图案区给字符 RAM 的偏移，名称表给显存偏移。 */
  [[nodiscard]] std::size_t locateVideo(std::uint16_t address, bool& isPattern) const;

 private:
  /** CPU 侧：$4020-$FFFF。 */
  class CpuSide final : public contracts::hardware::BusDevice {
   public:
    explicit CpuSide(Sb97Board& board) : board_(board) {}
    contracts::hardware::BusRead read(std::uint16_t address) override;
    contracts::hardware::BusRead peek(std::uint16_t address) override;
    void write(std::uint16_t address, std::uint8_t value) override;

   private:
    Sb97Board& board_;
  };

  /** PPU 侧：图案区与名称表。 */
  class VideoSide final : public contracts::hardware::BusDevice {
   public:
    explicit VideoSide(Sb97Board& board) : board_(board) {}
    contracts::hardware::BusRead read(std::uint16_t address) override;
    contracts::hardware::BusRead peek(std::uint16_t address) override;
    void write(std::uint16_t address, std::uint8_t value) override;

   private:
    Sb97Board& board_;
  };

  /** 读 CPU 总线；sideEffects 为 false 时是调试读：不碰有副作用的寄存器。 */
  contracts::hardware::BusRead readCpu(std::uint16_t address, bool sideEffects);
  void writeCpu(std::uint16_t address, std::uint8_t value);
  [[nodiscard]] bool programRamMode() const { return (programPage_ & 0x80U) != 0; }
  [[nodiscard]] bool thirtyTwoKilobyteMode() const { return (control_ & 0x04U) != 0; }
  [[nodiscard]] bool horizontalMirroring() const { return (control_ & 0x01U) != 0; }
  [[nodiscard]] bool tableSwitch() const { return (control_ & 0x03U) == 0x02U; }
  [[nodiscard]] std::size_t nametableOffset(std::uint16_t address) const;

  contracts::hardware::IrqLine& irq_;
  contracts::hardware::SignalSource irqSource_;
  FdcPortBridge floppy_;
  logic::hardware::SpeechLpc& speech_;

  CpuSide cpuSide_{*this};
  VideoSide videoSide_{*this};

  std::vector<std::uint8_t> rom_;
  std::vector<std::uint8_t> programRam_;
  std::vector<std::uint8_t> workRam_;
  std::vector<std::uint8_t> characterRam_;
  std::array<std::uint8_t, NAMETABLE_SIZE> nametable_{};

  std::uint8_t programPage_{0};
  std::uint8_t control_{0};
};

/** 软驱控制器的中断汇集器：控制器请求中断时让主板刷新 IRQ 线。 */
class Sb97FdcInterrupt final : public contracts::hardware::InterruptSink {
 public:
  void attach(Sb97Board& board) { board_ = &board; }
  void raise(std::uint8_t /*sourceMask*/) override {
    if (board_ != nullptr) board_->refreshFdcInterrupt();
  }

 private:
  Sb97Board* board_{nullptr};
};

}  // namespace ember::bootstrap
