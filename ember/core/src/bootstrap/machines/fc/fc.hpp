/*
 * ------------------------------------------------------------------------
 * 名称：fc.hpp
 * 说明：主机型号 fc（经典 FC，NTSC）的装配：2K 内部 RAM、2C02 PPU、2A03 APU、精灵 DMA、手柄口、卡槽、2K 名称表显存。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * 备注：时钟：主时钟 21.477272 MHz，CPU 12 分频、PPU 4 分频；CPU 总线与 PPU 总线的译码见 fc.cpp。
 *       具体实现只在这里创建并连线，模块之间不互相引用。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "bootstrap/machines/common/cpu-debug-kit.hpp"
#include "bootstrap/machines/common/pad-apu-port.hpp"
#include "bootstrap/machines/fc/fc-expansion-port.hpp"
#include "contracts/hardware/machine.hpp"
#include "logic/hardware/apu-2a03/apu-2a03.hpp"
#include "logic/hardware/cartridge-slot/standard-cartridge-slot.hpp"
#include "logic/hardware/cpu-6502/cpu-6502.hpp"
#include "logic/hardware/dma-2a03/dma-2a03.hpp"
#include "logic/hardware/bus/mapped-bus.hpp"
#include "logic/hardware/joypad/joypad.hpp"
#include "logic/hardware/lines/line-output.hpp"
#include "logic/hardware/lines/wired-or-line.hpp"
#include "logic/hardware/nametable-ram/nametable-ram.hpp"
#include "logic/hardware/pad-2a03/pad-2a03.hpp"
#include "logic/hardware/ppu-2c02/ppu-2c02.hpp"
#include "logic/hardware/ram/ram-block.hpp"
#include "logic/hardware/scheduler/master-scheduler.hpp"
#include "logic/hardware/tape-deck/tape-deck.hpp"

namespace ember::bootstrap {

/** 经典 FC（NTSC）。 */
class FcMachine final : public contracts::hardware::Machine, private contracts::hardware::MachineDebugger {
 public:
  /** 主时钟频率（赫兹）。 */
  static constexpr double MASTER_CLOCK_HZ = 21'477'272.0;
  /** 每个 CPU 周期的主时钟周期数。 */
  static constexpr contracts::hardware::MasterTime CPU_TICKS_PER_CYCLE = 12;
  /** 每个 PPU 点的主时钟周期数。 */
  static constexpr contracts::hardware::MasterTime PPU_TICKS_PER_DOT = 4;
  /** 上电后 PPU 第 0 点相对时间 0 的延迟（主时钟周期数），决定 CPU 与 PPU 的相位，按 blargg 的 PPU 时序测试调定。 */
  static constexpr contracts::hardware::MasterTime PPU_PHASE_TICKS = 1;

  /**
   * @param tapeDeck 磁带机：独立于主机的设备，由外面创建并在多台主机之间共用，主机通电时接上它；为空时主机自己带一台（测试用）。
   */
  explicit FcMachine(std::shared_ptr<logic::hardware::TapeDeck> tapeDeck = nullptr);
  ~FcMachine() override;

  FcMachine(const FcMachine&) = delete;
  FcMachine& operator=(const FcMachine&) = delete;

  void powerOn() override;
  void powerOff() override;
  void softReset() override;
  [[nodiscard]] bool isPoweredOn() const override { return poweredOn_; }
  void run(contracts::hardware::MasterTime ticks) override;
  void runFrame() override;
  [[nodiscard]] contracts::hardware::FrameBuffer frame() const override { return ppu_.frame(); }
  /** NTSC：每帧 262 行，开启背景的奇数帧少一个点，平均每帧 89341.5 个点。 */
  [[nodiscard]] double framesPerSecond() const override { return MASTER_CLOCK_HZ / (static_cast<double>(PPU_TICKS_PER_DOT) * (341.0 * 262.0 - 0.5)); }
  [[nodiscard]] unsigned audioSampleRate() const override { return logic::hardware::Apu2a03::SAMPLE_RATE; }
  void takeAudio(std::vector<float>& samples) override;
  void setButtons(unsigned port, std::uint8_t buttons) override;
  [[nodiscard]] unsigned padPortCount() const override { return static_cast<unsigned>(joypads_.size()); }
  void input(const contracts::hardware::InputEvent& event) override;
  [[nodiscard]] contracts::hardware::CpuRegisters cpuRegisters() const override { return cpu_.registers(); }
  [[nodiscard]] const contracts::hardware::Clock& clock() const override { return scheduler_; }
  contracts::hardware::CartridgeSlot& cartridgeSlot() override { return slot_; }
  contracts::hardware::IrqOutput& cartridgeIrq() override { return cartridgeIrq_; }
  contracts::hardware::MachineDebugger* debugger() override { return this; }
  contracts::hardware::TapeControl* tape() override { return tapeDeck_.get(); }
  contracts::hardware::TapeLine* tapeLine() override { return tapeDeck_.get(); }
  contracts::hardware::Scheduler* cartridgeScheduler() override { return &scheduler_; }
  [[nodiscard]] contracts::hardware::ClockDomain cartridgeCpuClock() const override { return contracts::hardware::ClockDomain{CPU_TICKS_PER_CYCLE}; }
  void connectFamilyKeyboard(bool connected) override;
  /** 接上 Family BASIC 键盘时，键盘属于机器；否则经典 FC 没有键盘。 */
  [[nodiscard]] bool keyboardIsMachineInput() const override { return expansionPort_.keyboardConnected(); }

  // 测试与调试用
  [[nodiscard]] logic::hardware::Ppu2c02& ppu() { return ppu_; }
  [[nodiscard]] const logic::hardware::Ppu2c02& ppu() const { return ppu_; }
  [[nodiscard]] logic::hardware::Apu2a03& apu() { return apu_; }
  [[nodiscard]] const logic::hardware::Apu2a03& apu() const { return apu_; }
  /** 2K 内部 RAM。 */
  [[nodiscard]] const contracts::hardware::StorageRegion& internalRam() const { return ram_; }
  /** 主板上的名称表显存（2K）。 */
  [[nodiscard]] const logic::hardware::NametableRam& nametableRam() const { return nametableRam_; }
  /** 不经 CPU 直接读 PPU 总线（图案表与名称表，不含调色板），读不改变 PPU 的状态。 */
  std::uint8_t peekPpuBus(std::uint16_t address) { return ppuBus_.read(address); }

 private:
  // MachineDebugger
  [[nodiscard]] std::vector<contracts::hardware::MemorySpace> memorySpaces() const override;
  void readMemory(std::string_view space, std::uint32_t address, std::span<std::uint8_t> out) override;
  void writeMemory(std::string_view space, std::uint32_t address, std::span<const std::uint8_t> bytes) override;
  [[nodiscard]] contracts::hardware::AddressTarget resolve(std::string_view space, std::uint32_t address) const override;
  [[nodiscard]] std::vector<std::string> stateModules() const override;
  [[nodiscard]] std::vector<contracts::hardware::StateField> readState(std::string_view module) const override;
  void writeState(std::string_view module, std::span<const contracts::hardware::StateField> fields) override;
  contracts::hardware::StepResult step(contracts::hardware::StepUnit unit, std::uint64_t count, bool stopAtBreakpoints) override;
  contracts::hardware::Breakpoint addBreakpoint(contracts::hardware::BreakpointKind kind, std::uint16_t first, std::uint16_t last, const std::string& condition) override { return debug_.addBreakpoint(kind, first, last, condition); }
  std::optional<contracts::hardware::Breakpoint> updateBreakpoint(std::uint32_t id, std::optional<bool> enabled, const std::optional<std::string>& condition) override { return debug_.updateBreakpoint(id, enabled, condition); }
  bool removeBreakpoint(std::uint32_t id) override { return debug_.removeBreakpoint(id); }
  std::size_t clearBreakpoints() override { return debug_.clearBreakpoints(); }
  [[nodiscard]] std::vector<contracts::hardware::Breakpoint> breakpoints() const override { return debug_.breakpoints(); }
  std::optional<contracts::hardware::BreakpointHit> takeBreakpointHit() override { return debug_.takeBreakpointHit(); }
  std::uint32_t startRecorder(const contracts::hardware::RecorderSpec& spec) override { return debug_.startRecorder(spec); }
  contracts::hardware::RecorderOutput stopRecorder(std::uint32_t id) override { return debug_.stopRecorder(id); }
  [[nodiscard]] contracts::hardware::RecorderSlice readRecorder(std::uint32_t id, std::uint64_t from, std::uint32_t maxLines) const override { return debug_.readRecorder(id, from, maxLines); }
  [[nodiscard]] std::vector<contracts::hardware::DisassembledInstruction> disassemble(std::uint16_t address, std::uint32_t count) override { return debug_.disassemble(address, count); }
  void startCdl(std::size_t size, contracts::hardware::CdlResolver resolver) override { debug_.startCdl(size, std::move(resolver)); }
  void stopCdl() override { debug_.stopCdl(); }
  [[nodiscard]] std::optional<std::vector<std::uint8_t>> cdlFlags() const override { return debug_.cdlFlags(); }
  [[nodiscard]] std::vector<contracts::hardware::CallFrame> callStack() const override { return debug_.callStack(); }
  void startProfiler() override { debug_.startProfiler(); }
  void stopProfiler() override { debug_.stopProfiler(); }
  [[nodiscard]] std::optional<contracts::hardware::ProfileReport> profile() const override { return debug_.profile(); }
  void startEventLog(std::uint32_t maxEvents) override { debug_.startEventLog(maxEvents); }
  void stopEventLog() override { debug_.stopEventLog(); }
  [[nodiscard]] std::optional<contracts::hardware::EventLogOutput> eventLog(std::optional<std::uint64_t> fromFrame, std::uint32_t maxEvents) const override { return debug_.eventLog(fromFrame, maxEvents); }
  [[nodiscard]] std::vector<std::uint32_t> colorTable() const override {
    const auto table = ppu_.colorTable();
    return {table.begin(), table.end()};
  }

  logic::hardware::MasterScheduler scheduler_;
  logic::hardware::WiredOrIrqLine irq_;
  logic::hardware::WiredOrIrqLine nmi_;
  logic::hardware::WiredOrRdyLine rdy_;
  /** 卡槽的 IRQ 引脚，在 IRQ 线上占一个固定来源。 */
  logic::hardware::LineIrqOutput cartridgeIrq_;
  logic::hardware::CpuAddressBus cpuBus_;
  logic::hardware::PpuAddressBus ppuBus_;

  logic::hardware::RamBlock ram_;
  logic::hardware::MirroredRam ramDevice_;
  logic::hardware::StandardCartridgeSlot slot_;
  logic::hardware::NametableRam nametableRam_;
  logic::hardware::Apu2a03 apu_;
  logic::hardware::Ppu2c02 ppu_;
  logic::hardware::Dma2a03 dma_;
  logic::hardware::Pad2a03 pad_;
  /** Family BASIC 数据录音机与声像磁带：磁带机，信号线接在 $4016；主机通电时才绑定自己的调度器。 */
  std::shared_ptr<logic::hardware::TapeDeck> tapeDeck_;
  /** $4016-$4017：手柄口与 APU 帧计数器共用，写入同时转给两边。 */
  PadApuPort padApuPort_;
  /** Family BASIC 键盘（接在扩展口上，默认没接）。 */
  logic::hardware::KeyboardFc keyboard_;
  /** $4016-$4017 的外设接口：在上面叠加磁带机与键盘的信号线。 */
  FcExpansionPort expansionPort_;
  std::array<logic::hardware::Joypad, logic::hardware::Pad2a03::PORT_COUNT> joypads_;
  logic::hardware::Cpu6502 cpu_;
  /** 断点、记录、代码数据标记与反汇编。 */
  CpuDebugKit debug_;

  bool poweredOn_{false};
};

}  // namespace ember::bootstrap
