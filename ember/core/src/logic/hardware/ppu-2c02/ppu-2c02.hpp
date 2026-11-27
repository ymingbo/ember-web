/*
 * ------------------------------------------------------------------------
 * 名称：ppu-2c02.hpp
 * 说明：经典 FC 的 PPU（2C02，NTSC）：寄存器、loopy 滚动模型、按点（dot）精度的背景与精灵渲染、vblank 与 NMI、帧缓冲输出。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * 备注：PPU 不逐周期被调用：CPU 访问寄存器前、自己的调度事件到期时，才把点时钟追赶到当前时间；
 *       点 k 在主时钟 phase + 4k 时执行，访问发生在同一时刻或更晚时，就能看到该点的结果；
 *       调度事件只在会改变 NMI 线的点（241 行第 1 点、预渲染行第 1 点与第 339 点）上登记，保证 CPU 逐周期采样 NMI 时不会漏掉变化；
 *       经 PpuBus 访问图案表与名称表，调色板在 PPU 内；不模拟开放总线的衰减与上电后的预热期。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <span>
#include <vector>

#include "contracts/hardware/clock.hpp"
#include "contracts/hardware/irq-line.hpp"
#include "contracts/hardware/ppu-bus.hpp"
#include "contracts/hardware/ppu.hpp"
#include "contracts/hardware/scheduler.hpp"

namespace ember::logic::hardware {

/** 经典 FC 的 PPU（2C02，NTSC）。 */
class Ppu2c02 final : public contracts::hardware::Ppu, public contracts::hardware::EventListener {
 public:
  /** 画面宽度（像素）。 */
  static constexpr std::uint32_t FRAME_WIDTH = 256;
  /** 画面高度（像素）。 */
  static constexpr std::uint32_t FRAME_HEIGHT = 240;
  /** 每行的点数。 */
  static constexpr int DOTS_PER_SCANLINE = 341;
  /** 每帧的行数（NTSC）。 */
  static constexpr int SCANLINES_PER_FRAME = 262;
  /** 预渲染行的行号。 */
  static constexpr int PRE_RENDER_LINE = 261;

  /** 可选的变体；不指定时是经典 FC 的 NTSC 2C02。 */
  struct Options {
    /** 每帧的行数，最后一行是预渲染行（NTSC 为 262，PAL 为 312）。 */
    int scanlinesPerFrame;
    /** 奇数帧且背景开启时预渲染行少一个点（NTSC 有，PAL 没有）。 */
    bool oddFrameSkip;
    /** vblank 置位的行（NTSC 与 PAL 为 241，Dendy 为 291，中间的行没有渲染）。 */
    int vblankLine{241};
    /**
     * 背景图案表的附加选择：参数是取这块背景图块时 v 的名称表序号（位 10-11，0 到 3，镜像之前的逻辑序号），
     * 返回 true 时这块背景图块用图案表 1（$1000 起），即使 $2000 位 4 为 0；返回 false 时按 $2000 位 4；
     * 不影响精灵；为空（默认）表示不启用，经典 FC 与其他机型都不设。
     */
    std::function<bool(unsigned)> backgroundTableOverride{};
    /**
     * 精灵取色的偏移：返回 true 时，精灵像素（颜色号 1 到 3）取色的位置整体后移 2 项，即取“所选精灵调色板第 2 + 颜色号项”；
     * 落在某个调色板的第 0 项（颜色号 2）时取背景色，超出 16 项的部分回绕；不影响背景；为空（默认）表示不启用。
     */
    std::function<bool()> spritePaletteShifted{};
    /**
     * 渲染中（背景或精灵开启，且在可见行或预渲染行）写 $2007 时，v 正被渲染推进，所写的地址不是程序设定的那个；
     * 为 true 时，这样写入图案表与名称表的数据一律丢弃（调色板区的写入照常），v 仍照常推进；为 false（默认）时照常写入 v 所指的地址。
     */
    bool dropVramWriteWhileRendering{false};
  };

  /**
   * @param scheduler 调度器，既是主时钟也登记 NMI 相关的事件，必须活到本对象销毁。
   * @param bus PPU 总线：图案表与名称表的访问经它完成。
   * @param nmi NMI 线，本对象在其上登记一个来源。
   * @param domain PPU 的时钟域，每个点占的主时钟周期数必须大于 0（NTSC 为 4）。
   * @param phase 上电或复位后第 0 点相对当前时间的延迟（主时钟周期数），不能为负；决定 CPU 与 PPU 的相位关系。
   */
  Ppu2c02(contracts::hardware::Scheduler& scheduler, contracts::hardware::PpuBus& bus, contracts::hardware::IrqLine& nmi, contracts::hardware::ClockDomain domain, contracts::hardware::MasterTime phase);

  /** 同上，另指定变体；vblank 起始行不能小于 241，每帧的行数必须大于它。 */
  Ppu2c02(contracts::hardware::Scheduler& scheduler, contracts::hardware::PpuBus& bus, contracts::hardware::IrqLine& nmi, contracts::hardware::ClockDomain domain, contracts::hardware::MasterTime phase, Options options);

  // BusDevice：$2000-$3FFF，地址每 8 个字节镜像一次
  contracts::hardware::BusRead read(std::uint16_t address) override;
  void write(std::uint16_t address, std::uint8_t value) override;

  // Ppu
  void reset(bool power) override;
  void catchUp(contracts::hardware::MasterTime now) override;

  /** 停止：取消调度事件并释放 NMI 线上的来源；主机型号把这颗 PPU 切走时调用，再 reset 就恢复运行。 */
  void halt();
  [[nodiscard]] contracts::hardware::FrameBuffer frame() const override;
  [[nodiscard]] std::uint64_t frameCount() const override { return frameCount_; }

  // EventListener
  void onEvent(contracts::hardware::MasterTime when, std::uint32_t tag) override;

  // 测试与调试用的只读访问（不追赶时间，不产生副作用）
  [[nodiscard]] int scanline() const { return scanline_; }
  [[nodiscard]] int dot() const { return dot_; }
  /** $2000 的当前取值。 */
  [[nodiscard]] std::uint8_t control() const { return ctrl_; }
  /** $2001 的当前取值。 */
  [[nodiscard]] std::uint8_t mask() const { return mask_; }
  /** $2002 的当前取值（位 7 vblank、位 6 精灵 0 命中、位 5 精灵溢出，低 5 位为 0）；读它不清标志。 */
  [[nodiscard]] std::uint8_t status() const { return static_cast<std::uint8_t>((vblank_ ? 0x80 : 0) | (sprite0Hit_ ? 0x40 : 0) | (spriteOverflow_ ? 0x20 : 0)); }
  /**
   * 64 个颜色号现在显示的颜色，按当前的颜色强调与灰度位换算，每项内存顺序 R、G、B、A（小端整数为 0xAABBGGRR）。
   */
  [[nodiscard]] std::array<std::uint32_t, 64> colorTable() const;
  /** 追赶到 now 之后的扫描位置，不改变 PPU 的状态；奇数帧预渲染行少一个点按现在的背景开关算。 */
  [[nodiscard]] contracts::hardware::VideoPosition positionAt(contracts::hardware::MasterTime now) const;
  [[nodiscard]] std::uint16_t vramAddress() const { return v_; }
  [[nodiscard]] std::uint16_t tempAddress() const { return t_; }
  [[nodiscard]] std::uint8_t fineX() const { return fineX_; }
  [[nodiscard]] bool writeToggle() const { return w_; }
  [[nodiscard]] bool vblankFlag() const { return vblank_; }
  /** 背景或精灵至少一个开启（主板按它决定是否计扫描线）。 */
  [[nodiscard]] bool renderingEnabled() const { return (mask_ & 0x18) != 0; }
  [[nodiscard]] bool spriteZeroHit() const { return sprite0Hit_; }
  [[nodiscard]] bool spriteOverflow() const { return spriteOverflow_; }
  [[nodiscard]] std::span<const std::uint8_t> oam() const { return oam_; }
  [[nodiscard]] std::span<const std::uint8_t> secondaryOam() const { return secondary_; }
  [[nodiscard]] std::span<const std::uint8_t> palette() const { return palette_; }
  /** 当前每行的精灵数（下一行要画的精灵）。 */
  [[nodiscard]] int lineSpriteCount() const { return spriteCount_; }
  /** 调试写入：直接改 OAM 的一个字节，不经寄存器。 */
  void pokeOam(std::size_t index, std::uint8_t value) { oam_.at(index) = value; }
  /** 调试写入：直接改调色板的一个字节（索引 0 到 31，不做镜像处理）。 */
  void pokePalette(std::size_t index, std::uint8_t value) { palette_.at(index) = value & 0x3FU; }

 private:
  // 寄存器与渲染的内部步骤见 ppu-2c02.cpp（寄存器、时间）与 ppu-2c02-render.cpp（渲染）
  std::uint8_t readStatus();
  std::uint8_t readOamData();
  std::uint8_t readData();
  void writeScroll(std::uint8_t value);
  void writeAddress(std::uint8_t value);
  void writeOamData(std::uint8_t value);
  void writeData(std::uint8_t value);
  void incrementVramAddress();
  void updateNmi();
  void scheduleNextEvent();
  void runDot();
  void publishFrame();

  [[nodiscard]] std::uint8_t paletteIndex(std::uint16_t address) const;

  // 渲染
  void shiftBackground();
  void fetchBackground(int dot);
  void incrementCoarseX();
  void incrementY();
  void loadBackgroundShifters();
  void evaluateSprites(int line, int dot);
  void fetchSprites(int line, int dot);
  void drawPixel(int line, int x);
  [[nodiscard]] std::uint8_t readVram(std::uint16_t address);
  /** 当前这块背景图块用的图案表基址（$0000 或 $1000）。 */
  [[nodiscard]] std::uint16_t backgroundTableBase() const;

  contracts::hardware::Scheduler& scheduler_;
  contracts::hardware::PpuBus& bus_;
  contracts::hardware::IrqLine& nmi_;
  contracts::hardware::SignalSource nmiSource_;
  contracts::hardware::ClockDomain domain_;
  contracts::hardware::MasterTime phase_;
  Options options_;
  /** 预渲染行的行号（每帧行数减 1）。 */
  int preRenderLine_;
  /** vblank 置位那一点在一帧里的序号。 */
  int vblankStartIndex_;
  contracts::hardware::EventId eventId_{contracts::hardware::NO_EVENT};

  // 时间：下一个要执行的点与它开始的主时钟时间
  contracts::hardware::MasterTime nextDotTime_{0};
  int scanline_{0};
  int dot_{0};
  bool oddFrame_{false};
  /** 上一个点执行完时背景是否开启。 */
  bool backgroundWasEnabled_{false};
  std::uint64_t frameCount_{0};

  // 寄存器
  std::uint8_t ctrl_{0};
  std::uint8_t mask_{0};
  bool vblank_{false};
  bool sprite0Hit_{false};
  bool spriteOverflow_{false};
  /** 读 $2002 发生在 vblank 置位前一个点：本帧不置位。 */
  bool suppressVbl_{false};
  std::uint8_t oamAddr_{0};
  std::uint8_t readBuffer_{0};
  /** PPU 数据总线上最近一次的值，读只写寄存器与 $2002 低 5 位时返回。 */
  std::uint8_t openBus_{0};

  // loopy 滚动模型
  std::uint16_t v_{0};
  std::uint16_t t_{0};
  std::uint8_t fineX_{0};
  bool w_{false};

  // 存储
  std::array<std::uint8_t, 256> oam_{};
  std::array<std::uint8_t, 32> secondary_{};
  std::array<std::uint8_t, 32> palette_{};

  // 背景流水线
  std::uint8_t nextTileId_{0};
  std::uint8_t nextAttribute_{0};
  std::uint8_t nextPatternLow_{0};
  std::uint8_t nextPatternHigh_{0};
  std::uint16_t shiftPatternLow_{0};
  std::uint16_t shiftPatternHigh_{0};
  std::uint16_t shiftAttributeLow_{0};
  std::uint16_t shiftAttributeHigh_{0};

  // 精灵评估：n 是精灵号，m 是精灵内的字节号
  std::uint8_t oamBuffer_{0};
  std::uint8_t secondaryAddress_{0};
  std::uint8_t spriteN_{0};
  std::uint8_t spriteM_{0};
  bool spriteInRange_{false};
  bool evaluationDone_{false};
  bool sprite0Added_{false};
  int overflowCounter_{0};
  int nextSpriteCount_{0};
  bool nextSprite0_{false};

  // 当前行要画的精灵（在上一行的第 257-320 点取好）
  struct SpriteUnit {
    std::uint8_t patternLow{0};
    std::uint8_t patternHigh{0};
    std::uint8_t attribute{0};
    std::uint8_t x{0};
  };
  std::array<SpriteUnit, 8> sprites_{};
  int spriteCount_{0};
  bool sprite0InLine_{false};

  // 输出：两块帧缓冲轮流绘制与显示
  std::array<std::vector<std::uint32_t>, 2> frameBuffers_;
  int drawIndex_{0};
  std::array<std::uint32_t, 512> rgbaTable_{};
};

}  // namespace ember::logic::hardware
