/*
 * ------------------------------------------------------------------------
 * 名称：pyramid-board.hpp
 * 说明：金字塔学习机（PEC-9588、PEC-586、PEC-686F）主板上的存储与寄存器：512K 的程序 ROM、8K 的工作 RAM、8K 的字符 RAM、2K 的名称表显存、汉字显示电路与磁带线。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-21
 * 备注：这是主机型号自己的胶合逻辑，只由 pyramid.cpp 创建并挂到总线上；两个型号共用寄存器与外设，只有 $8000-$FFFF 的程序窗口规则不同；
 *       CPU 地址 $4020-$FFFF：
 *         $4100-$5FFF 写：存入寄存器 reg[(地址>>8)&7]，随后按 reg0 重新计算镜像、汉字模式与程序窗口；
 *           reg0 位 7 是汉字模式；(reg0&$18)==$18 名称表水平镜像，否则垂直镜像；
 *         $5100 写：磁带的输出线——PEC-9588 取位 0，PEC-586 与 PEC-686F 取位 2；位 1 是打印机的选通线，从低变高时打印机取走 $5200 里的数据（没有接打印机时忽略）；磁带的输入线：PEC-9588 在 $5500 读出值的位 2（$5500 读出 $10，位 2 叠加输入线），
 *           PEC-586 与 PEC-686F 在 $5300 读出值的位 2（只驱动位 2 与位 0，其余位是开放总线，位 0 是打印机就绪：接着打印机且不忙时为 1；$5500 读出 $10）；其余 $4020-$5FFF 读到开放总线；
 *           PEC-9588 的打印机就绪线在 $4017 的位 2，由主机装配里的输入口叠加，主板只给出 printerReady；
 *         $6000-$7FFF：8K 工作 RAM；$8000-$FFFF：程序窗口，只读，写入忽略；
 *       PEC-9588 的程序窗口（16K 块，ROM 共 32 块）：由 reg0&$70 决定：$00 是 $8000 块 reg0&$F、$C000 块 31；$20 是 $8000 块 (reg0&$F)|$10、$C000 块 31；
 *         $40 是 $8000 块 reg0&$F、$C000 块 30；$60 是 $8000 块 (reg0&$F)|$10、$C000 块 30（reg0 大于等于 $6C 时是块 28）；位 4 为 1 的值不改变窗口；复位后 $8000 块 0、$C000 块 31；
 *       PEC-586 与 PEC-686F 的程序窗口：上电与复位后整个 $8000-$FFFF 是一块 32K 的“启动映像”（由 ROM 里每 16K 块末尾的两段 1K 拼成，见 pyramid-board.cpp），
 *         reg0 位 4 为 1 时整个窗口换成 ROM 的第 reg0&7 个 32K；位 4 为 0 时 reg0&$70 是 $40 或 $60 就把 $8000-$9FFF 换成 ROM 的第 (reg0&$F)|$20 或 (reg0&$F)|$30 个 8K，其余仍是启动映像；
 *       PPU 地址：$0000-$1FFF 是字符 RAM，$2000-$3EFF 是名称表显存（2K，按 reg0 镜像）；汉字模式下背景的图案读取按名称表地址改写（见 pyramid-text.hpp）；
 *       $5600-$56FF：接着 5.25 英寸软驱控制器时（只有 PEC-9588），整页是软驱控制器的 16 个寄存器，
 *         地址的低 4 位是寄存器号，每 16 字节镜像一次（磁盘操作系统用 $5610-$561F），
 *         见 logic/hardware/disk2/disk2.hpp；写入同时也存进 reg6；调试读不碰软驱控制器；
 *       上电后寄存器为 0，存储清零。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "contracts/hardware/bus-device.hpp"
#include "contracts/hardware/parallel-port.hpp"
#include "contracts/hardware/tape-deck.hpp"
#include "logic/hardware/disk2/disk2.hpp"
#include "logic/hardware/pyramid-text/pyramid-text.hpp"

namespace ember::bootstrap {

/** 金字塔学习机主板。 */
class PyramidBoard final {
 public:
  /** 主板型号：决定程序窗口的规则。 */
  enum class Model : std::uint8_t { Pec9588, Pec586 };

  /** 程序 ROM 的容量（字节）。 */
  static constexpr std::size_t ROM_SIZE = 512 * 1024;
  /** 一个 8K 页的大小。 */
  static constexpr std::size_t ROM_PAGE_SIZE = 8 * 1024;
  /** ROM 的 8K 页数。 */
  static constexpr std::size_t PAGE_COUNT = ROM_SIZE / ROM_PAGE_SIZE;
  /** 程序窗口的页数（$8000-$FFFF）。 */
  static constexpr std::size_t WINDOW_COUNT = 4;
  /** 启动映像的容量（字节）。 */
  static constexpr std::size_t BOOT_IMAGE_SIZE = ROM_PAGE_SIZE * WINDOW_COUNT;
  /** 工作 RAM 的容量（字节），在 $6000-$7FFF。 */
  static constexpr std::size_t WORK_RAM_SIZE = 8 * 1024;
  /** 字符 RAM 的容量（字节）。 */
  static constexpr std::size_t CHARACTER_RAM_SIZE = 8 * 1024;
  /** 名称表显存的容量（字节）。 */
  static constexpr std::size_t NAMETABLE_SIZE = 2048;
  /** 寄存器的个数。 */
  static constexpr std::size_t REGISTER_COUNT = 8;

  /** CPU 地址对应的存储位置。 */
  struct CpuLocation {
    enum class Kind : std::uint8_t { None, Register, Rom, BootImage, WorkRam };
    Kind kind{Kind::None};
    std::size_t offset{0};
  };

  /**
   * @param model 主板型号。
   * @param rom 程序 ROM 的内容，大小必须等于 ROM_SIZE（不含 iNES 文件头）。
   * @param tape 磁带机的信号线：$5100 写位 0 驱动输出，读 $5500 位 2 取输入。
   * @param printer 并口打印机，必须活到主板销毁；为空时没有接打印机：数据丢弃、就绪线读 0。
   * @param disk2 5.25 英寸软驱控制器，必须活到主板销毁；为空时没有接软驱：$5600-$56FF 读到开放总线。
   * @throws shared::CoreError hardware.invalidArgument：ROM 大小不对。
   */
  PyramidBoard(Model model, std::vector<std::uint8_t> rom, contracts::hardware::TapeLine& tape, contracts::hardware::ParallelPort* printer = nullptr, logic::hardware::Disk2* disk2 = nullptr);

  PyramidBoard(const PyramidBoard&) = delete;
  PyramidBoard& operator=(const PyramidBoard&) = delete;

  /** CPU 总线上 $4020-$FFFF 的设备。 */
  contracts::hardware::BusDevice& cpuSide() { return cpuSide_; }
  /** PPU 总线上 $0000-$3EFF 的设备。 */
  contracts::hardware::BusDevice& videoSide() { return videoSide_; }

  /**
   * 复位：寄存器回到 0、程序窗口回到初始状态；上电时存储清零，软复位时存储保持。
   * @param power 是否上电。
   */
  void reset(bool power);

  // 调试与测试用
  [[nodiscard]] Model model() const { return model_; }
  [[nodiscard]] const std::vector<std::uint8_t>& rom() const { return rom_; }
  [[nodiscard]] const std::array<std::uint8_t, BOOT_IMAGE_SIZE>& bootImage() const { return bootImage_; }
  [[nodiscard]] const std::vector<std::uint8_t>& workRam() const { return workRam_; }
  [[nodiscard]] std::vector<std::uint8_t>& workRam() { return workRam_; }
  [[nodiscard]] const std::vector<std::uint8_t>& characterRam() const { return characterRam_; }
  [[nodiscard]] std::vector<std::uint8_t>& characterRam() { return characterRam_; }
  [[nodiscard]] const std::array<std::uint8_t, NAMETABLE_SIZE>& nametable() const { return nametable_; }
  [[nodiscard]] std::array<std::uint8_t, NAMETABLE_SIZE>& nametable() { return nametable_; }
  [[nodiscard]] const std::array<std::uint8_t, REGISTER_COUNT>& registers() const { return registers_; }
  [[nodiscard]] bool textMode() const { return text_.textMode(); }
  /** 打印机就绪：接着打印机且打印机不忙。 */
  [[nodiscard]] bool printerReady() const { return printer_ != nullptr && printer_->busyN(); }
  [[nodiscard]] bool horizontalMirroring() const { return (registers_[0] & MIRROR_MASK) == MIRROR_MASK; }
  /** CPU 地址当前对应的存储位置。 */
  [[nodiscard]] CpuLocation locate(std::uint16_t address) const;
  /** PPU 地址（$0000-$3EFF）当前对应的位置：图案区给字符 RAM 的偏移，名称表给显存偏移。 */
  [[nodiscard]] std::size_t locateVideo(std::uint16_t address, bool& isPattern) const;

 private:
  /** 位 3 与位 4 同时为 1 时名称表水平镜像。 */
  static constexpr std::uint8_t MIRROR_MASK = 0x18;

  /** 程序窗口里的一个 8K 页指向 ROM 或启动映像的哪一页。 */
  struct WindowPage {
    bool bootImage{false};
    std::size_t offset{0};
  };

  /** CPU 侧：$4020-$FFFF。 */
  class CpuSide final : public contracts::hardware::BusDevice {
   public:
    explicit CpuSide(PyramidBoard& board) : board_(board) {}
    contracts::hardware::BusRead read(std::uint16_t address) override;
    contracts::hardware::BusRead peek(std::uint16_t address) override;
    void write(std::uint16_t address, std::uint8_t value) override;

   private:
    PyramidBoard& board_;
  };

  /** PPU 侧：图案区与名称表。 */
  class VideoSide final : public contracts::hardware::BusDevice {
   public:
    explicit VideoSide(PyramidBoard& board) : board_(board) {}
    contracts::hardware::BusRead read(std::uint16_t address) override;
    contracts::hardware::BusRead peek(std::uint16_t address) override;
    void write(std::uint16_t address, std::uint8_t value) override;

   private:
    PyramidBoard& board_;
  };

  /** 读 CPU 总线；sideEffects 为 false 时是调试读：不推进磁带机。 */
  contracts::hardware::BusRead readCpu(std::uint16_t address, bool sideEffects);
  void writeCpu(std::uint16_t address, std::uint8_t value);
  /** 按 reg0 重新计算汉字模式与程序窗口。 */
  void applyRegisters();
  void applyWindows9588();
  void applyWindows586();
  /** 把窗口 first 与 first+1 设成 ROM 的一个 16K 块。 */
  void setRomBlock(std::size_t first, std::size_t block);
  void setRomPage(std::size_t window, std::size_t page);
  void setBootPage(std::size_t window);
  /** 586 的启动映像：从 ROM 每个 16K 块的末尾取两段 1K 拼成。 */
  void buildBootImage();
  [[nodiscard]] std::size_t nametableOffset(std::uint16_t address) const;

  Model model_;
  contracts::hardware::TapeLine& tape_;
  contracts::hardware::ParallelPort* printer_;
  logic::hardware::Disk2* disk2_;

  CpuSide cpuSide_{*this};
  VideoSide videoSide_{*this};
  logic::hardware::PyramidText text_;

  std::vector<std::uint8_t> rom_;
  std::array<std::uint8_t, BOOT_IMAGE_SIZE> bootImage_{};
  std::vector<std::uint8_t> workRam_;
  std::vector<std::uint8_t> characterRam_;
  std::array<std::uint8_t, NAMETABLE_SIZE> nametable_{};

  std::array<std::uint8_t, REGISTER_COUNT> registers_{};
  std::array<WindowPage, WINDOW_COUNT> windows_{};
};

}  // namespace ember::bootstrap
