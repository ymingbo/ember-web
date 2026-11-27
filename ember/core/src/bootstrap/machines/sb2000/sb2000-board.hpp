/*
 * ------------------------------------------------------------------------
 * 名称：sb2000-board.hpp
 * 说明：主机型号 sb2000（小霸王 2000）主板上的存储与胶合逻辑：程序窗口、显存、$4100-$43FF 的超级 I/O、$4400-$5FFF 的工作窗口。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-15
 * 备注：这些是主机型号自己的胶合逻辑（不属于任何芯片模块），所以放在主机装配旁边，只由 sb2000.cpp 创建并挂到总线上；
 *       存储的容量与位置按真机实测：BIOS ROM 512KB、扩展 RAM 512KB、字符 RAM 32KB、扩展显存 512KB（经 $4300 选 32KB 的页）。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <vector>

#include "contracts/hardware/bus-device.hpp"
#include "bootstrap/machines/common/fdc-port-bridge.hpp"
#include "logic/hardware/fdc-upd765/fdc-upd765.hpp"
#include "logic/hardware/paging-um6576/paging-um6576.hpp"
#include "logic/hardware/ram/ram-block.hpp"
#include "logic/hardware/speech-lpc/speech-lpc.hpp"
#include "logic/hardware/sysinit-um6576/sysinit-um6576.hpp"

namespace ember::bootstrap {

/** 程序窗口 $8000-$FFFF：按分页寄存器把每 4K 对应到 BIOS ROM 或扩展 RAM。 */
class Sb2000ProgramMemory final : public contracts::hardware::BusDevice {
 public:
  /** BIOS ROM 的容量（字节）。 */
  static constexpr std::size_t ROM_SIZE = 512 * 1024;
  /** 扩展 RAM 的容量（字节）。 */
  static constexpr std::size_t EXTENDED_RAM_SIZE = 512 * 1024;

  /**
   * @param paging 分页寄存器。
   * @param rom BIOS ROM，必须是 ROM_SIZE 字节，调用方保证它活到本对象销毁。
   */
  Sb2000ProgramMemory(const logic::hardware::PagingUm6576& paging, std::span<const std::uint8_t> rom) : paging_(paging), rom_(rom), extendedRam_(EXTENDED_RAM_SIZE, 0) {}

  contracts::hardware::BusRead read(std::uint16_t address) override { return contracts::hardware::driveBus(peekByte(address)); }

  contracts::hardware::BusRead peek(std::uint16_t address) override { return read(address); }

  /** ROM 页的写入被丢弃，扩展 RAM 页可写。 */
  void write(std::uint16_t address, std::uint8_t value) override {
    const logic::hardware::PagingUm6576::Location location = paging_.resolve(address);
    if (location.ram) extendedRam_[location.offset] = value;
  }

  /** 扩展 RAM。 */
  [[nodiscard]] std::vector<std::uint8_t>& extendedRam() { return extendedRam_; }
  [[nodiscard]] const std::vector<std::uint8_t>& extendedRam() const { return extendedRam_; }

  /** 上电：扩展 RAM 清零。 */
  void clear() { std::fill(extendedRam_.begin(), extendedRam_.end(), std::uint8_t{0}); }

  /** 无副作用地读一个 CPU 地址（不小于 $8000）。 */
  [[nodiscard]] std::uint8_t peekByte(std::uint16_t address) const {
    const logic::hardware::PagingUm6576::Location location = paging_.resolve(address);
    return location.ram ? extendedRam_[location.offset] : rom_[location.offset];
  }

 private:
  const logic::hardware::PagingUm6576& paging_;
  std::span<const std::uint8_t> rom_;
  std::vector<std::uint8_t> extendedRam_;
};

/** 显存：地址 $0000-$7FFF 是字符 RAM（32KB），$8000-$FFFF 是扩展显存的 32KB 窗口（页由 $4300 选择）。 */
class Sb2000VideoMemory final : public contracts::hardware::BusDevice {
 public:
  /** 字符 RAM 的容量（字节）。 */
  static constexpr std::size_t CHARACTER_RAM_SIZE = 32 * 1024;
  /** 扩展显存的容量（字节）。 */
  static constexpr std::size_t EXTENDED_VRAM_SIZE = 512 * 1024;
  /** 扩展显存窗口的大小（字节）。 */
  static constexpr std::size_t WINDOW_SIZE = 32 * 1024;

  Sb2000VideoMemory() : characterRam_(CHARACTER_RAM_SIZE, 0), extendedVram_(EXTENDED_VRAM_SIZE, 0) {}

  contracts::hardware::BusRead read(std::uint16_t address) override { return contracts::hardware::driveBus(*locate(address)); }

  contracts::hardware::BusRead peek(std::uint16_t address) override { return read(address); }

  void write(std::uint16_t address, std::uint8_t value) override { *locate(address) = value; }

  /** 选择扩展显存的 32KB 页（$4300 的低 4 位）。 */
  void selectBank(std::uint8_t value) { bank_ = value & 0x0F; }
  [[nodiscard]] std::uint8_t bank() const { return bank_; }

  /** 上电：全部清零，页回到 0。 */
  void clear() {
    std::fill(characterRam_.begin(), characterRam_.end(), std::uint8_t{0});
    std::fill(extendedVram_.begin(), extendedVram_.end(), std::uint8_t{0});
    bank_ = 0;
  }

  [[nodiscard]] std::vector<std::uint8_t>& characterRam() { return characterRam_; }
  [[nodiscard]] const std::vector<std::uint8_t>& characterRam() const { return characterRam_; }
  [[nodiscard]] std::vector<std::uint8_t>& extendedVram() { return extendedVram_; }
  [[nodiscard]] const std::vector<std::uint8_t>& extendedVram() const { return extendedVram_; }

 private:
  std::uint8_t* locate(std::uint16_t address) {
    if (address < CHARACTER_RAM_SIZE) return &characterRam_[address];
    return &extendedVram_[static_cast<std::size_t>(bank_) * WINDOW_SIZE + (address & 0x7FFFU)];
  }

  std::vector<std::uint8_t> characterRam_;
  std::vector<std::uint8_t> extendedVram_;
  std::uint8_t bank_{0};
};

/**
 * 超级 I/O 窗口 $4100-$43FF：软驱控制器（$4100 写数据率、$4101 写 DOR、$4200-$4207）、显存页寄存器 $4300、FC 兼容模式开关 $4301（写 $55 时通知主机型号，主机型号在指令结束后切换）、语音端口 $4302（转给语音芯片）；
 * 其余地址读出 0，写入被丢弃。
 * 软驱端口（$4200-$4207）：0 与 1 是 DMA 数据口（写 1 同时给出终止计数），2 读出位 6 是数据请求，3 读出位 6 是中断标志，
 * 4 读主状态、写位 6 是控制器复位脚，5 读写数据，7 读盘片更换位、写数据率，2 写 DOR，6 读出 0。
 */
class Sb2000SuperIo final : public contracts::hardware::BusDevice {
 public:
  Sb2000SuperIo(Sb2000VideoMemory& video, logic::hardware::FdcUpd765& floppy, logic::hardware::SpeechLpc& speech) : video_(video), floppy_(floppy), speech_(speech) {}

  contracts::hardware::BusRead read(std::uint16_t address) override {
    if (address >= FIRST_FLOPPY_PORT && address <= LAST_FLOPPY_PORT) return contracts::hardware::driveBus(floppy_.readPcPort(address & 7U, true).value_or(0));
    if (address == ADDRESS_SPEECH) return speech_.read(address);
    return contracts::hardware::driveBus(0);
  }

  contracts::hardware::BusRead peek(std::uint16_t address) override {
    if (address >= FIRST_FLOPPY_PORT && address <= LAST_FLOPPY_PORT) return contracts::hardware::driveBus(floppy_.readPcPort(address & 7U, false).value_or(0));
    return read(address);
  }

  void write(std::uint16_t address, std::uint8_t value) override {
    if (address == ADDRESS_DATA_RATE) {
      floppy_.writeRegister(7, value);
    } else if (address == ADDRESS_DOR) {
      floppy_.writeRegister(2, value);
    } else if (address >= FIRST_FLOPPY_PORT && address <= LAST_FLOPPY_PORT) {
      floppy_.writePcPort(address & 7U, value);
    } else if (address == ADDRESS_VIDEO_BANK) {
      video_.selectBank(value);
    } else if (address == ADDRESS_FC_MODE) {
      if (value == FC_MODE_KEY && onFcMode) onFcMode();
    } else if (address == ADDRESS_SPEECH) {
      speech_.write(address, value);
    }
  }

  /** $4301 写入 $55（进入 FC 兼容模式）时调用；在 CPU 总线访问里被调用，不能在里面直接复位 CPU。 */
  std::function<void()> onFcMode;

 private:
  static constexpr std::uint16_t ADDRESS_DATA_RATE = 0x4100;
  static constexpr std::uint16_t ADDRESS_DOR = 0x4101;
  static constexpr std::uint16_t FIRST_FLOPPY_PORT = 0x4200;
  static constexpr std::uint16_t LAST_FLOPPY_PORT = 0x4207;
  static constexpr std::uint16_t ADDRESS_VIDEO_BANK = 0x4300;
  static constexpr std::uint16_t ADDRESS_FC_MODE = 0x4301;
  static constexpr std::uint8_t FC_MODE_KEY = 0x55;
  static constexpr std::uint16_t ADDRESS_SPEECH = 0x4302;

  Sb2000VideoMemory& video_;
  FdcPortBridge floppy_;
  logic::hardware::SpeechLpc& speech_;
};

/**
 * 工作窗口 $4400-$5FFF：6576 模式（向 $4031 写过 $65、$76）下不是 RAM，读出 $68、写入无效（真机实测）；
 * 6576 模式之前：$4400-$4FFF 没有器件应答，$5000-$5FFF 是 2K 工作 RAM（与内部 RAM 同一块，镜像两次）；$5006 是 128K 窗口选择（FC 兼容模式用），两种模式下写入都有效果。
 */
class Sb2000WorkWindow final : public contracts::hardware::BusDevice {
 public:
  /**
   * @param mode 初始设置模块，读它得知是否已进入 6576 模式。
   * @param ram 内部 RAM（2K）。
   */
  Sb2000WorkWindow(const logic::hardware::SysinitUm6576& mode, contracts::hardware::StorageRegion& ram) : mode_(mode), ram_(ram) {}

  contracts::hardware::BusRead read(std::uint16_t address) override {
    if (mode_.enabled()) return contracts::hardware::driveBus(FIXED_VALUE);
    if (address < FIRST_RAM) return contracts::hardware::releaseBus();
    return contracts::hardware::driveBus(ram_.read(address % ram_.size()));
  }

  contracts::hardware::BusRead peek(std::uint16_t address) override { return read(address); }

  void write(std::uint16_t address, std::uint8_t value) override {
    // $5006 选 128K 窗口，不论是否在 6576 模式都有效（真机实测：6576 模式下这一段写入无效，$5006 的写入仍有效果）。
    if (address == ADDRESS_WINDOW_SELECT && onWindowSelect) onWindowSelect(value);
    if (mode_.enabled() || address < FIRST_RAM) return;
    ram_.write(address % ram_.size(), value);
  }

  /** 写 $5006 时调用，参数是写入的值。 */
  std::function<void(std::uint8_t)> onWindowSelect;

 private:
  static constexpr std::uint16_t FIRST_RAM = 0x5000;
  static constexpr std::uint16_t ADDRESS_WINDOW_SELECT = 0x5006;
  static constexpr std::uint8_t FIXED_VALUE = 0x68;

  const logic::hardware::SysinitUm6576& mode_;
  contracts::hardware::StorageRegion& ram_;
};

}  // namespace ember::bootstrap
