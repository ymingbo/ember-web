/*
 * ------------------------------------------------------------------------
 * 名称：yuxing-board.hpp
 * 说明：主机型号 yuxing（裕兴学习机）主板上的存储与整机寄存器：1MB 的 ROM、1MB 的程序 RAM、MMC3 模式专用的 8K 工作 RAM、128K 的字符 RAM、2K 的名称表显存，以及 $4020-$5FFF 的寄存器、MMC3 模式、汉字单色模式与 4 分屏。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-21
 * 备注：这些是主机型号自己的胶合逻辑（不属于任何芯片模块），只由 yuxing.cpp 创建并挂到总线上；
 *       CPU 地址 $4020-$FFFF：
 *         $4020-$40FF 读出 0、写入忽略；
 *         $4200 写软驱寄存器 7（数据率）、$4201 写寄存器 2（DOR）、$4205 写寄存器 5（数据）；$4304 读寄存器 4（主状态）、$4305 读寄存器 5（数据）；
 *         软驱控制器还有一组别名：地址满足 (地址 & $FE4F) == $4244 时（如 $4254 DOR、$4364 主状态、$42E4 与 $43E4 数据、$42F4 数据率），寄存器号由地址位拼出：位 0 取地址位 7，位 1 取地址位 4，位 2 取地址位 5，读写与上面一样；
 *         $4202、$4302、$5004 写键盘行选择的低 8 位，$4203、$4303、$5005 写高 6 位，$4207 读键盘；
 *         $4700 写语音芯片（0 复位，$FF 开始收数据，$C0-$CF 各带半个字节，先低半后高半，合成一个字节后异或 $41 送入语音芯片），$4701 读语音芯片的忙闲（忙读 $80）；
 *         $4800 写 reg4800：位 0-4 选 32K 的 ROM 页，位 5 为 1 时 $8000 起的写入是程序 RAM 的页寄存器，位 7 打开分屏；
 *         $5002 读 $02；
 *         $5500 写 reg5500（$55xx 的偶数地址都是它）：位 0-1 选 $6000 窗口（程序 RAM 的 8K 页 (($3C & 程序 RAM 掩码) | 位 0-1)，位 0-1 为 0 时就是 $3C & 掩码那一页），位 2 为 1 时程序窗口用程序 RAM，位 3 名称表水平镜像，位 6 为 1 时程序 RAM 按 32K 取页，位 7 在分屏时选 4 分屏；
 *         $5501 写字符页：位 0-3 选 8K 字符页，位 7 为 1 进入 MMC3 模式；
 *         $6000-$7FFF 窗口；$8000-$FFFF 程序窗口：
 *           程序 RAM 模式（reg5500 位 2）：位 6 为 1 时整个窗口是第 (页寄存器>>1) 个 32K，否则 $8000-$BFFF 是第“页寄存器”个 16K（写入页寄存器时取低位与程序 RAM 掩码相与）、$C000-$FFFF 固定是第“程序 RAM 掩码”个 16K；
 *           ROM 模式：整个窗口是 ROM 的第 (reg4800&$1F) 个 32K；写入忽略；
 *           MMC3 模式：窗口由 MMC3 电路给页（程序页号取低 6 位，落在程序 RAM 的前 512K），写入全部交给 MMC3 电路；reg5500 位 2 为 1 时 $6000-$7FFF 是电路自己的 8K 工作 RAM（与程序 RAM 无关），位 2 为 0 时 $6000 窗口与 ROM 窗口照常；
 *       型号：按 ROM 的 CRC-32 认型号，决定程序 RAM 掩码（16K 页号的掩码）与字符页掩码（8K 页号的掩码）：V7.0、V8.0-C、V8.2-D、V8.3-D 是 $07 与 $03（程序 RAM 128K、字符 RAM 32K），V9.0-98 与不认识的 ROM 是 $3F 与 $0F（程序 RAM 1MB、字符 RAM 128K）；
 *       PPU 地址：$0000-$1FFF 是字符 RAM（非 MMC3 模式整体 8K 一页，MMC3 模式按电路的 1K 页），$2000-$2FFF 是名称表显存（2K，水平或垂直镜像）；
 *       汉字单色模式（不在 MMC3 模式、分屏且 reg5500 位 7 为 0）由 yuxing-text 电路改写背景取字模的地址，同时精灵取色整体后移 2 项（spritePaletteShifted）；4 分屏（不在 MMC3 模式、分屏且 reg5500 位 7 为 1）时，背景与精灵读 $1000-$1FFF 的 4K 页取自 8K 页
 *       ((最近名称表字节地址的位 9、8) << 2 | (reg5501 & 3)) 的上半，其余情况 $1000-$1FFF 照常取 reg5501 所选页；
 *       上电后寄存器为 $00（$5002 读 $02），存储清零，窗口是 ROM 的第 0 个 32K。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "contracts/hardware/bus-device.hpp"
#include "contracts/hardware/mapper-circuit.hpp"
#include "bootstrap/machines/common/fdc-port-bridge.hpp"
#include "logic/hardware/fdc-upd765/fdc-upd765.hpp"
#include "logic/hardware/keyboard-yuxing/keyboard-yuxing.hpp"
#include "logic/hardware/yuxing-text/yuxing-text.hpp"
#include "logic/hardware/speech-lpc/speech-lpc.hpp"

namespace ember::bootstrap {

/** 裕兴学习机主板。 */
class YuxingBoard final {
 public:
  /** ROM 的容量（字节）：32 个 32K 页。 */
  static constexpr std::size_t ROM_SIZE = 1024 * 1024;
  /** 程序 RAM 的容量（字节）。 */
  static constexpr std::size_t PROGRAM_RAM_SIZE = 1024 * 1024;
  /** MMC3 模式下 $6000-$7FFF 用的工作 RAM 的容量（字节）。 */
  static constexpr std::size_t MMC3_WORK_RAM_SIZE = 8 * 1024;
  /** 小容量型号的 $6000 窗口页选择 1-3 用的静态 RAM 的容量（字节）。 */
  static constexpr std::size_t STATIC_RAM_SIZE = 32 * 1024;
  /** 字符 RAM 的容量（字节）。 */
  static constexpr std::size_t CHARACTER_RAM_SIZE = 128 * 1024;
  /** 名称表显存的容量（字节）。 */
  static constexpr std::size_t NAMETABLE_SIZE = 2048;

  /** 寄存器的快照，供调试。 */
  struct Registers {
    std::uint8_t reg4800;
    std::uint8_t reg5500;
    std::uint8_t reg5501;
    std::uint8_t pageRegister;
    std::uint16_t keyboardRows;
    bool mmc3Mode;
    bool horizontalMirroring;
    bool textMode;
    bool fourWaySplit;
  };

  /** 一个型号的存储容量：程序 RAM 掩码是 16K 页号的掩码，字符页掩码是 8K 页号的掩码。 */
  struct MemoryMasks {
    std::uint8_t programRam;
    std::uint8_t character;
  };

  /** CPU 地址对应的存储位置。 */
  struct CpuLocation {
    enum class Kind : std::uint8_t { None, Register, Rom, ProgramRam, Mmc3WorkRam, StaticRam };
    Kind kind{Kind::None};
    std::size_t offset{0};
  };

  /**
   * @param rom ROM 的内容，大小必须等于 ROM_SIZE。
   * @param fdc 软驱控制器。
   * @param speech 语音芯片（裕兴接线变体）。
   * @param keyboard 键盘。
   * @param mmc3 MMC3 电路（程序页与字符页由它给出）。
   * @throws shared::CoreError hardware.invalidArgument：ROM 大小不对。
   */
  YuxingBoard(std::vector<std::uint8_t> rom, logic::hardware::FdcUpd765& fdc, logic::hardware::SpeechLpc& speech, logic::hardware::KeyboardYuxing& keyboard, contracts::hardware::MapperCircuit& mmc3);

  YuxingBoard(const YuxingBoard&) = delete;
  YuxingBoard& operator=(const YuxingBoard&) = delete;

  /** 汉字单色模式下精灵取色整体后移 2 项（PPU 渲染时查询）。 */
  [[nodiscard]] bool spritePaletteShifted() const { return textMode(); }

  /** 按 ROM 的 CRC-32 给出型号的存储容量：V7.0、V8.0-C、V8.2-D、V8.3-D 是 128K 程序 RAM 与 32K 字符 RAM，其余（含 V9.0-98）是 1MB 与 128K。 */
  [[nodiscard]] static MemoryMasks memoryMasksFor(std::uint32_t romCrc32);

  /** CPU 总线上 $4020-$FFFF 的设备。 */
  contracts::hardware::BusDevice& cpuSide() { return cpuSide_; }
  /** PPU 总线上 $0000-$3EFF 的设备。 */
  contracts::hardware::BusDevice& videoSide() { return videoSide_; }

  /**
   * 复位：寄存器回到初始值，窗口回到 ROM 的第 0 个 32K；上电时存储清零，软复位时存储保持。
   * @param power 是否上电。
   */
  void reset(bool power);

  // 调试与测试用
  [[nodiscard]] const std::vector<std::uint8_t>& rom() const { return rom_; }
  [[nodiscard]] const std::vector<std::uint8_t>& programRam() const { return programRam_; }
  [[nodiscard]] std::vector<std::uint8_t>& programRam() { return programRam_; }
  [[nodiscard]] const std::vector<std::uint8_t>& mmc3WorkRam() const { return mmc3WorkRam_; }
  [[nodiscard]] std::vector<std::uint8_t>& mmc3WorkRam() { return mmc3WorkRam_; }
  [[nodiscard]] const std::vector<std::uint8_t>& staticRam() const { return staticRam_; }
  [[nodiscard]] std::vector<std::uint8_t>& staticRam() { return staticRam_; }
  [[nodiscard]] const std::vector<std::uint8_t>& characterRam() const { return characterRam_; }
  [[nodiscard]] std::vector<std::uint8_t>& characterRam() { return characterRam_; }
  [[nodiscard]] const std::array<std::uint8_t, NAMETABLE_SIZE>& nametable() const { return nametable_; }
  [[nodiscard]] std::array<std::uint8_t, NAMETABLE_SIZE>& nametable() { return nametable_; }
  [[nodiscard]] Registers registers() const;
  /** CPU 地址当前对应的存储位置。 */
  [[nodiscard]] CpuLocation locate(std::uint16_t address) const;
  /** PPU 地址（$0000-$3EFF）当前对应的位置：图案区给字符 RAM 的偏移，名称表给显存偏移（调试用，不含汉字模式与 4 分屏的取指改写）。 */
  [[nodiscard]] std::size_t locateVideo(std::uint16_t address, bool& isPattern) const;

 private:
  /** CPU 侧：$4020-$FFFF。 */
  class CpuSide final : public contracts::hardware::BusDevice {
   public:
    explicit CpuSide(YuxingBoard& board) : board_(board) {}
    contracts::hardware::BusRead read(std::uint16_t address) override;
    contracts::hardware::BusRead peek(std::uint16_t address) override;
    void write(std::uint16_t address, std::uint8_t value) override;

   private:
    YuxingBoard& board_;
  };

  /** PPU 侧：图案区与名称表。 */
  class VideoSide final : public contracts::hardware::BusDevice {
   public:
    explicit VideoSide(YuxingBoard& board) : board_(board) {}
    contracts::hardware::BusRead read(std::uint16_t address) override;
    contracts::hardware::BusRead peek(std::uint16_t address) override;
    void write(std::uint16_t address, std::uint8_t value) override;

   private:
    YuxingBoard& board_;
  };

  /** 一个 8K 窗口接到哪块存储的哪个偏移。 */
  struct Window {
    CpuLocation::Kind kind{CpuLocation::Kind::Rom};
    std::size_t base{0};
  };

  /** 读 CPU 总线；sideEffects 为 false 时是调试读：不碰有副作用的寄存器。 */
  contracts::hardware::BusRead readCpu(std::uint16_t address, bool sideEffects);
  void writeCpu(std::uint16_t address, std::uint8_t value);
  void writeLow(std::uint16_t address, std::uint8_t value);
  void writeProgram(std::uint16_t address, std::uint8_t value);
  void writeSpeech(std::uint8_t value);

  /** 按 reg4800、reg5500 与页寄存器重排 $6000 起的窗口与名称表镜像。 */
  void applyBanks();
  /** MMC3 电路的寄存器变化后重排 $8000-$FFFF 的 4 个窗口。 */
  void syncMmc3Windows();
  void setProgramRamWindows(std::size_t firstWindow, std::size_t first8kPage, std::size_t count);
  void setRomWindows(std::size_t first8kPage);
  void updateTextMode();

  [[nodiscard]] bool programRamMode() const { return (reg5500_ & 0x04U) != 0; }
  [[nodiscard]] bool splitEnabled() const { return (reg4800_ & 0x80U) != 0; }
  [[nodiscard]] bool textMode() const { return !mmc3Mode_ && splitEnabled() && (reg5500_ & 0x80U) == 0; }
  [[nodiscard]] bool fourWaySplit() const { return !mmc3Mode_ && splitEnabled() && (reg5500_ & 0x80U) != 0; }
  /** 图案地址（$0000-$1FFF）在字符 RAM 里的偏移；latched 为真时 4 分屏按最近的名称表字节地址选页。 */
  [[nodiscard]] std::size_t characterOffset(std::uint16_t address, bool latched) const;
  [[nodiscard]] std::size_t nametableOffset(std::uint16_t address) const;

  FdcPortBridge floppy_;
  logic::hardware::SpeechLpc& speech_;
  logic::hardware::KeyboardYuxing& keyboard_;
  contracts::hardware::MapperCircuit& mmc3_;
  logic::hardware::YuxingText text_;

  CpuSide cpuSide_{*this};
  VideoSide videoSide_{*this};

  std::vector<std::uint8_t> rom_;
  std::vector<std::uint8_t> programRam_;
  std::vector<std::uint8_t> mmc3WorkRam_;
  std::vector<std::uint8_t> staticRam_;
  std::vector<std::uint8_t> characterRam_;
  std::array<std::uint8_t, NAMETABLE_SIZE> nametable_{};

  /** 型号决定的 16K 程序页号掩码与 8K 字符页号掩码。 */
  std::uint8_t programRamMask_{0x3F};
  std::uint8_t characterMask_{0x0F};

  /** $6000-$FFFF 的 5 个 8K 窗口（序号 = (地址 >> 13) - 3）。 */
  std::array<Window, 5> windows_{};

  std::uint8_t reg4800_{0};
  std::uint8_t reg5500_{0};
  std::uint8_t reg5501_{0};
  /** $8000 起写入给出的程序 RAM 页寄存器。 */
  std::uint8_t pageRegister_{0};
  bool mmc3Mode_{false};
  bool horizontalMirroring_{false};

  // 语音芯片的数据协议
  bool speechReceiving_{false};
  unsigned speechNibbles_{0};
  std::uint8_t speechByte_{0};
};

}  // namespace ember::bootstrap
