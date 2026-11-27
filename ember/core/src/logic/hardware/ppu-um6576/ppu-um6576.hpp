/*
 * ------------------------------------------------------------------------
 * 名称：ppu-um6576.hpp
 * 说明：6576 的 PPU（图形端口）：寄存器 $2000-$2008，调色板 $2040-$207F，16 位显存地址，4 色或 16 色背景，每个名称表项 2 字节。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-15
 * 备注：按扫描行渲染：每行开始时用当时的寄存器画完整一行（不逐点），行与行之间由调度器事件推进；
 *       PAL 时序：每行 341 点、每帧 312 行，第 0-239 行可见，第 240 行开始时发布一帧，第 241 行开始置垂直消隐标志（并按 $2000 位 7 发 NMI），
 *       第 311 行（预渲染）开始时清除 $2002 的位 7、6、5；
 *       寄存器行为已按真机实测（hwtest）修正，见 docs/02 与 sb2000 硬件说明：
 *       $2000 位 7 NMI、位 5 精灵 8x16、位 2 地址增量、位 1-0 背景页；$2001 位 0 黑白、位 1 与位 2 为 1 时显示最左 8 个像素（极性同 NES）、
 *       位 3 背景、位 4 精灵、位 5 增强绿、位 6 增强红、位 7 增强蓝；$2002 读：位 7 垂直消隐、位 6 精灵 0 碰撞、位 5 一行超过 8 个精灵，
 *       低 5 位是最后一次写 $2000-$2007 的值，读后清位 7 与写锁存；$2004 读属性字节时位 4-2 恒为 1；$2008 位 7 为 1 是 16 色、位 3-2 精灵图案表、位 0 为 1 时名称表从 $2000 起且只有一页；
 *       调色板 64 项只保存低 6 位，读出时位 6 为 1，颜色号为 0 的像素显示调色板第 0 项，背景关闭时画面是颜色 $00；
 *       颜色按 6 位色号（低 4 位色相、位 5-4 亮度）取通用的 NES 调色板；精灵每行不限制个数，超过 8 个时只置位 $2002 位 5；
 *       不模拟 OAM 的刷新缺陷与精灵 0 命中的像素级时序。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include "contracts/hardware/irq-line.hpp"
#include "contracts/hardware/ppu-bus.hpp"
#include "contracts/hardware/ppu.hpp"
#include "contracts/hardware/scheduler.hpp"

namespace ember::logic::hardware {

/** 6576 的 PPU。 */
class PpuUm6576 final : public contracts::hardware::Ppu, public contracts::hardware::EventListener {
 public:
  /** 画面宽度（像素）。 */
  static constexpr std::uint32_t FRAME_WIDTH = 256;
  /** 画面高度（像素）。 */
  static constexpr std::uint32_t FRAME_HEIGHT = 240;
  /** 每行的点数。 */
  static constexpr int DOTS_PER_SCANLINE = 341;
  /** 每帧的行数（PAL）。 */
  static constexpr int SCANLINES_PER_FRAME = 312;
  /** 第一条垂直消隐行。 */
  static constexpr int VBLANK_LINE = 241;
  /** 预渲染行。 */
  static constexpr int PRE_RENDER_LINE = 311;

  /**
   * @param scheduler 调度器，行的推进靠它的事件。
   * @param bus 显存总线：16 位地址，图案表与名称表的访问经它完成（地址 $8000 以上是扩展显存）。
   * @param nmi NMI 线，本对象在其上登记一个来源。
   * @param domain PPU 的时钟域，每个点占的主时钟周期数（PAL 为 5）。
   * @param phase 上电或复位后第 0 行开始相对当前时间的延迟（主时钟周期数）。
   */
  PpuUm6576(contracts::hardware::Scheduler& scheduler, contracts::hardware::PpuBus& bus, contracts::hardware::IrqLine& nmi, contracts::hardware::ClockDomain domain, contracts::hardware::MasterTime phase);

  // BusDevice：$2000-$207F
  contracts::hardware::BusRead read(std::uint16_t address) override;
  contracts::hardware::BusRead peek(std::uint16_t address) override;
  void write(std::uint16_t address, std::uint8_t value) override;

  // Ppu
  void reset(bool power) override;
  void catchUp(contracts::hardware::MasterTime /*now*/) override {}

  /** 停止：取消每行的调度事件并释放 NMI 线上的来源；主机型号切到 FC 兼容模式时调用，再 reset 就恢复运行。 */
  void halt() {
    if (eventId_ != contracts::hardware::NO_EVENT) scheduler_.cancel(eventId_);
    eventId_ = contracts::hardware::NO_EVENT;
    nmi_.set(nmiSource_, false);
  }
  [[nodiscard]] contracts::hardware::FrameBuffer frame() const override;
  [[nodiscard]] std::uint64_t frameCount() const override { return frameCount_; }

  // EventListener
  void onEvent(contracts::hardware::MasterTime when, std::uint32_t tag) override;

  /** 精灵 DMA 的写入：把一个字节写入 OAM（经 $2004，地址随之递增）。 */
  void writeOamData(std::uint8_t value) { write(0x2004, value); }

  // 测试与调试用的只读访问
  /** 下一个要处理的行号。 */
  [[nodiscard]] int nextLine() const { return line_; }
  /** 扫描位置：这颗 PPU 按行处理，位置是下一个要处理的行，点恒为 0；不追赶时间，不产生副作用。 */
  [[nodiscard]] contracts::hardware::VideoPosition position() const {
    // frameCount_ 在第 240 行处理时加 1，下一行已经越过它时要减回来才是帧序号。
    return contracts::hardware::VideoPosition{frameCount_ - (line_ > static_cast<int>(FRAME_HEIGHT) ? 1U : 0U), line_, 0};
  }
  /** 64 个颜色号现在显示的颜色，按当前的黑白与增强位换算，每项内存顺序 R、G、B、A（小端整数为 0xAABBGGRR）。 */
  [[nodiscard]] std::array<std::uint32_t, 64> colorTable() const;
  [[nodiscard]] std::uint16_t vramAddress() const { return address_; }
  [[nodiscard]] std::uint8_t control() const { return ctrl_; }
  [[nodiscard]] std::uint8_t mask() const { return mask_; }
  [[nodiscard]] std::uint8_t status() const { return status_; }
  [[nodiscard]] std::uint8_t reg2008() const { return reg2008_; }
  [[nodiscard]] std::span<const std::uint8_t> oam() const { return oam_; }
  [[nodiscard]] std::span<const std::uint8_t> palette() const { return palette_; }
  /** 调试写入：直接改 OAM 的一个字节。 */
  void pokeOam(std::size_t index, std::uint8_t value) { oam_.at(index) = value; }
  /** 调试写入：直接改调色板的一个字节（索引 0 到 63）。 */
  void pokePalette(std::size_t index, std::uint8_t value) { palette_.at(index) = value & 0x3FU; }

 private:
  /** 处理一行：渲染、发布一帧、置位或清除垂直消隐。 */
  void processLine();
  /** 用当前寄存器画一行（索引 0 到 239）到当前绘制缓冲。 */
  void renderLine(int line);
  /** 把行缓冲里的 6 位色号按 $2001 的黑白与增强位转成 RGB 写进画面。 */
  void storeLine(int line);
  void updateNmi();
  void scheduleNextLine();

  contracts::hardware::Scheduler& scheduler_;
  contracts::hardware::PpuBus& bus_;
  contracts::hardware::IrqLine& nmi_;
  contracts::hardware::SignalSource nmiSource_;
  contracts::hardware::ClockDomain domain_;
  contracts::hardware::MasterTime phase_;
  contracts::hardware::EventId eventId_{contracts::hardware::NO_EVENT};
  contracts::hardware::MasterTime nextLineTime_{0};
  int line_{0};
  std::uint64_t frameCount_{0};

  std::uint8_t ctrl_{0};
  std::uint8_t mask_{0};
  std::uint8_t status_{0};
  std::uint8_t oamAddress_{0};
  std::uint8_t reg2008_{0};
  std::uint8_t scrollX_{0};
  std::uint8_t scrollY_{0};
  bool scrollToggle_{false};
  bool addressToggle_{false};
  std::uint16_t address_{0};
  std::uint8_t readBuffer_{0};
  /** 最后一次写 $2000-$2007 的值，读 $2002 时作为低 5 位。 */
  std::uint8_t lastWrite_{0};
  std::array<std::uint8_t, 256> oam_{};
  std::array<std::uint8_t, 64> palette_{};

  /** 行缓冲：6 位色号，左边多 8 个像素的余量。 */
  std::array<std::uint8_t, 272> lineBuffer_{};
  std::array<std::uint8_t, 272> backgroundOpaque_{};
  std::array<std::uint8_t, 272> spriteOccupied_{};
  /** 画面：绘制缓冲与已发布的缓冲。 */
  std::vector<std::uint32_t> drawing_;
  std::vector<std::uint32_t> published_;
};

}  // namespace ember::logic::hardware
