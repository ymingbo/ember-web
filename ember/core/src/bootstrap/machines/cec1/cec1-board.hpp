/*
 * ------------------------------------------------------------------------
 * 名称：cec1-board.hpp
 * 说明：中华学习机 CEC-I 的主板：64K 主存、语言卡的 16K 存储、两块 32K 的 ROM（U7 与 U35）、
 *       可选的汉字字库 ROM（U33 与 U34）、$C000-$C0FF 的软开关、内置软驱接口、
 *       $C0B0 寄存器与槽 1 的打印机接口卡；整个 CPU 地址空间都由本板译码。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-25
 * 备注：ROM 文件是 64K：前 32K 是 U7（低 16K 是 LOGO 语言，高 16K 对应 $C000-$FFFF：
 *       $C100-$CFFF 是内部 ROM、$D000-$FFFF 是监控与 CEC-BASIC），
 *       后 32K 是 U35（低 16K 是汉字表，高 16K 对应 $C000-$FFFF：$C300 与 $C600 是槽 ROM、
 *       $D000-$FFFF 是汉字程序）；后面可以再接 256K 的汉字字库：先是 128K 的 U33（字模的左半，
 *       偶地址），再是 128K 的 U34（字模的右半，奇地址），都是芯片里的原始内容；
 *       辅存就是这些 ROM：辅存的 $0000-$3FFF 与主存是同一块 RAM，$4000-$7FFF 是汉字字库，
 *       $8000-$BFFF 是代码表，$D000-$FFFF 是汉字程序；
 *       CPU 读的译码（见 locateRead）：
 *         $0000-$3FFF 主存；$4000-$BFFF 主存，辅存读开关（$C002/$C003）为 1 时是 ROM：
 *         $4000-$7FFF 读汉字字库：$C0B0 的低 4 位选 16K 的页（bank），芯片地址是页号乘 8192 加上
 *         页内偏移除 2，偶偏移读 U33、奇偏移读 U34，CPU 读到的字节与芯片里的位序相反；
 *         没有接字库时读内置字库（见 cec1-font.hpp）；$8000-$BFFF 读 U7 低 16K（LOGO，
 *         $C0B0 的位 5 为 1）
 *         或 U35 低 16K（汉字表，位 5 为 0）；
 *         $C000-$C0FF 软开关与 I/O；$C100-$CFFF：内部 ROM 开关（$C006/$C007）为 1 时读 U7 高 16K，
 *         否则读 U35 高 16K（槽 ROM），其中 $C3xx 在槽 3 开关（$C00A/$C00B）为 0 时也读 U7；
 *         $D000-$FFFF：语言卡读 RAM 时，辅助零页开关（$C008/$C009）为 1 读 U35 高 16K（汉字程序），
 *         为 0 读语言卡存储；语言卡读 ROM 时辅助零页开关不起作用，读 U7 高 16K；
 *       写：$0000-$3FFF 写主存；$4000-$BFFF 写主存，但辅存写开关为 1 时写进了 ROM，丢弃；
 *       $D000-$FFFF 语言卡写允许且辅助零页开关为 0 时写语言卡存储，其余写入忽略；
 *       语言卡（$C080-$C08F，读与写都算访问）：A3 为 0 选 bank 2（$D000-$DFFF），为 1 选 bank 1；
 *       A1A0 为 00 与 11 读 RAM、01 与 10 读 ROM；写允许就是 A0（奇数地址允许写、偶数地址禁止），
 *       按硬件手册，不需要连续两次访问；
 *       软开关：$C000-$C00F 写：偶数地址关、奇数地址开：80 列存储、辅存读、辅存写、内部 ROM、
 *       辅助零页、槽 3 ROM、80 列显示、备用字符集；
 *       $C000 读键盘，$C010 读写清键盘标志，$C011-$C01F 读状态（位 7），
 *       其中 $C019 的位 7 在垂直消隐期间为 0、显示期间为 1；$C020 磁带输出翻转，
 *       $C030 扬声器翻转，$C050-$C057 显示开关，
 *       $C060 磁带输入、$C061-$C063 游戏接口的三个按键（按键 0 与 1 是手柄的 A 与 B 和
 *       两个苹果键，按键 2 是手柄的选择与开始或按着的“西文”键，1.21 的复位程序据此冷启动）、
 *       $C064-$C067 模拟手柄 0 到 3 的计时，$C070 触发模拟手柄计时；
 *       $C0E0-$C0EF 内置软驱接口（槽 6）；$C0AE、$C0AF 内置串口（槽 2，状态位 1 恒为发送就绪，
 *       读不到数据）；
 *       $C0B0 寄存器（槽 3 的设备选择区）：读不驱动数据线；写入的值整个保存，复位值 $10，
 *       位 0 到 3 是字库页，位 4 为 1 允许彩色、为 0 显示单色，位 5 选 LOGO 或汉字表（见上）；
 *       槽 1 的打印机接口卡：$C090 写数据（同时产生选通，字节原样交给打印机），
 *       $C091 是 Ember 的扩展文本口（字节交给文本适配排成位图行再交给打印机），
 *       $C1C0-$C1C7 在内部 ROM 开关为 0 时读状态：地址 0 的位 7 是 Ack，地址 4 的位 7 是忙，
 *       其余地址的位 7 为 0；$C100 起的 26 字节是按接口卡行为重写的驱动程序（PR#1 用：
 *       等不忙、写 $C090、回车后追加换行），原卡的驱动 ROM 没有收集到，
 *       $C100-$C1FF 的其余部分读到 U35 里的内容。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "contracts/hardware/bus-device.hpp"
#include "contracts/hardware/clock.hpp"
#include "contracts/hardware/parallel-port.hpp"
#include "contracts/hardware/tape-deck.hpp"
#include "logic/hardware/disk2/disk2.hpp"
#include "logic/hardware/keyboard-apple2/keyboard-apple2.hpp"
#include "logic/hardware/printer-text/printer-text.hpp"
#include "logic/hardware/speaker-1bit/speaker-1bit.hpp"
#include "logic/hardware/video-apple2/video-apple2.hpp"

namespace ember::bootstrap {

/** 中华学习机 CEC-I 的主板。 */
class Cec1Board final : public contracts::hardware::BusDevice, public logic::hardware::Apple2VideoSource {
 public:
  /** ROM 文件的大小：U7 与 U35 各 32K。 */
  static constexpr std::size_t ROM_SIZE = 64 * 1024;
  /**
   * 可选的汉字字库 ROM（U33 与 U34 各 128K，合起来是 16 个 16K 的页）：接在 ROM 文件的后面时，
   * 文件是 ROM_SIZE + LIBRARY_SIZE，U33 在前、U34 在后。
   */
  static constexpr std::size_t LIBRARY_SIZE = 256 * 1024;
  static constexpr std::size_t LIBRARY_CHIP_SIZE = 128 * 1024;
  static constexpr std::size_t LIBRARY_BANK_SIZE = 16 * 1024;
  /** 主存的大小。 */
  static constexpr std::size_t RAM_SIZE = 64 * 1024;
  /** 语言卡存储的大小：bank 1 的 4K、bank 2 的 4K 与 $E000-$FFFF 的 8K。 */
  static constexpr std::size_t LANGUAGE_CARD_SIZE = 16 * 1024;
  /** ROM 里 U7 低 16K、U7 高 16K、U35 低 16K、U35 高 16K 的起点。 */
  static constexpr std::size_t U7_LOW = 0x0000;
  static constexpr std::size_t U7_HIGH = 0x4000;
  static constexpr std::size_t U35_LOW = 0x8000;
  static constexpr std::size_t U35_HIGH = 0xC000;

  /** 软开关与寄存器的状态。 */
  struct Switches {
    bool text{true};
    bool mixed{false};
    bool page2{false};
    bool hires{false};
    bool store80{false};
    bool ramRead{false};
    bool ramWrite{false};
    bool internalRom{false};
    bool altZeroPage{false};
    bool slotThreeRom{false};
    bool column80{false};
    bool altCharset{false};
    /** 语言卡：读 RAM、写允许、选 bank 2。 */
    bool lcReadRam{false};
    bool lcWriteRam{false};
    bool lcBank2{true};
    /** 四个信号输出。 */
    std::array<bool, 4> annunciators{};
    /** $C0B0 寄存器，复位值 $10。 */
    std::uint8_t aux{0x10};
  };

  /** CPU 地址所在的位置，调试用。 */
  struct Location {
    enum class Kind : std::uint8_t { Ram, LanguageCard, Rom, Font, PrinterRom, Register, None };
    Kind kind{Kind::None};
    /**
     * 存储区里的偏移：Rom 是 ROM 文件里的偏移，Font 是内置字库里的偏移，
     * PrinterRom 是打印机卡驱动程序里的偏移，
     * Register 是 $C000 起的偏移。
     */
    std::size_t offset{0};
  };

  /**
   * @param rom ROM 的内容，必须是 ROM_SIZE 字节（U7 在前、U35 在后，不含文件头），
   * 或后面再接 LIBRARY_SIZE 字节的汉字字库。
   * @param clock 主时钟：扬声器与模拟手柄的计时用它。
   * @param keyboard 键盘。
   * @param disk 内置软驱接口（槽 6）。
   * @param speaker 扬声器。
   * @param tape 磁带线（$C020 输出、$C060 输入）。
   * @param parallel 槽 1 打印机接口卡接的并口。
   * @param cpuTicksPerCycle 每个 CPU 周期的主时钟周期数（模拟手柄的计时按 CPU 周期换算）。
   * @throws shared::CoreError hardware.invalidArgument：ROM 大小不对。
   */
  Cec1Board(std::vector<std::uint8_t> rom, const contracts::hardware::Clock& clock, logic::hardware::KeyboardApple2& keyboard, logic::hardware::Disk2& disk, logic::hardware::Speaker1Bit& speaker,
            contracts::hardware::TapeLine& tape, contracts::hardware::ParallelPort& parallel, contracts::hardware::MasterTime cpuTicksPerCycle);

  /**
   * 复位。
   * @param power 上电时主存与语言卡存储清零，软开关与寄存器回到默认值；
   * 软复位只让软开关与寄存器回到默认值。
   */
  void reset(bool power);

  // BusDevice
  contracts::hardware::BusRead read(std::uint16_t address) override;
  void write(std::uint16_t address, std::uint8_t value) override;
  contracts::hardware::BusRead peek(std::uint16_t address) override;

  // Apple2VideoSource
  [[nodiscard]] logic::hardware::Apple2VideoMode videoMode() const override;
  [[nodiscard]] std::uint8_t readVideoByte(std::uint16_t address) const override { return ram_[address]; }

  /** 接上视频生成器，$C019 的垂直消隐位从它取；必须活到本对象销毁。 */
  void setVideo(const logic::hardware::VideoApple2& video) { video_ = &video; }

  /**
   * 模拟手柄与按键：按手柄的按键掩码设置（方向键对应模拟手柄 0 的横向与手柄 1 的纵向，
   * A 与 B 是按键 0 与 1，选择与开始是按键 2）。
   */
  void setPaddles(std::uint8_t buttons);

  /** CPU 读这个地址时读到的位置。 */
  [[nodiscard]] Location locateRead(std::uint16_t address) const;

  [[nodiscard]] const Switches& switches() const { return switches_; }
  [[nodiscard]] const std::vector<std::uint8_t>& rom() const { return rom_; }
  [[nodiscard]] std::vector<std::uint8_t>& ram() { return ram_; }
  [[nodiscard]] const std::vector<std::uint8_t>& ram() const { return ram_; }
  [[nodiscard]] std::vector<std::uint8_t>& languageCard() { return languageCard_; }
  [[nodiscard]] const std::vector<std::uint8_t>& languageCard() const { return languageCard_; }

 private:
  /** 读 $C000-$C0FF；side 为假时不产生副作用。 */
  contracts::hardware::BusRead readIo(std::uint8_t reg, bool side);
  void writeIo(std::uint8_t reg, std::uint8_t value);
  /** $C000-$C00F 的写。 */
  void writeSwitch(std::uint8_t reg);
  /** 语言卡的一次访问（读或写）。 */
  void accessLanguageCard(std::uint8_t reg);
  /** 显示开关（$C050-$C057）的访问。 */
  void accessVideoSwitch(std::uint8_t reg);
  /** $C011-$C01F 的状态位 7。 */
  [[nodiscard]] bool statusFlag(std::uint8_t reg) const;
  /** 语言卡存储里 CPU 地址（$D000-$FFFF）对应的偏移。 */
  [[nodiscard]] std::size_t languageCardOffset(std::uint16_t address) const;
  /** 模拟手柄 n 的计时是否还没结束。 */
  [[nodiscard]] bool paddleTiming(unsigned paddle) const;
  /** 汉字字库里 $4000-$7FFF 的地址对应的 ROM 文件偏移。 */
  [[nodiscard]] std::size_t libraryOffset(std::uint16_t address) const;
  /** 内置字库里 $4000-$7FFF 的地址对应的偏移。 */
  [[nodiscard]] std::size_t builtinFontOffset(std::uint16_t address) const;
  /** 打印机接口卡的状态寄存器（$C1C0-$C1C7）；index 是低 4 位。 */
  [[nodiscard]] std::uint8_t printerStatus(std::uint8_t index) const;

  const contracts::hardware::Clock& clock_;
  logic::hardware::KeyboardApple2& keyboard_;
  logic::hardware::Disk2& disk_;
  logic::hardware::Speaker1Bit& speaker_;
  contracts::hardware::TapeLine& tape_;
  contracts::hardware::ParallelPort& parallel_;
  /** 把 $C091 收到的文本排成位图行再送给并口。 */
  logic::hardware::PrinterText text_;
  contracts::hardware::MasterTime cpuTicksPerCycle_;
  const logic::hardware::VideoApple2* video_{nullptr};

  std::vector<std::uint8_t> rom_;
  std::vector<std::uint8_t> ram_;
  std::vector<std::uint8_t> languageCard_;
  Switches switches_;

  /** 磁带输出线当前的电平。 */
  bool tapeOutput_{false};
  /** 手柄按键：按键 0 到 2，以及四个模拟手柄的值（0 到 255）。 */
  bool button0_{false};
  bool button1_{false};
  bool button2_{false};
  std::array<std::uint8_t, 4> paddleValues_{128, 128, 128, 128};
  /** 最近一次触发模拟手柄计时的时间。 */
  contracts::hardware::MasterTime paddleTrigger_{-1'000'000'000};
};

}  // namespace ember::bootstrap
