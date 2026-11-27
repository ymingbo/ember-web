/*
 * ------------------------------------------------------------------------
 * 名称：sb97.hpp
 * 说明：主机型号 sb97（小霸王 97 学习机，Dendy 制式）的装配：2A03 的 CPU 与 APU、2C02 的 PPU、精灵 DMA、手柄口 1、键盘、软驱控制器、语音芯片与主板。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-17
 * 备注：时钟：主时钟 26,601,712 Hz，CPU 15 分频（约 1.7734 MHz），PPU 5 分频（每个 CPU 周期 3 个点），每行 341 点、每帧 312 行、vblank 从第 291 行起（50 Hz）；
 *       CPU 总线与 PPU 总线的译码见 sb97.cpp 与 sb97-board.hpp；整机机型没有卡带，Machine 契约要求的卡槽是一个空的、不挂载的卡槽；
 *       软驱控制器的中断接 CPU 的 IRQ 线；APU 的帧 IRQ 在上电与复位时被禁止（写 $4017=$C0）；没有鼠标、没有打印机；
 *       语音芯片的输出按主时钟时间混进 APU 的声音；PPU 的背景图案表选择由主板按 $5200 与名称表序号决定；
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
#include <utility>
#include <vector>

#include "bootstrap/machines/common/cpu-debug-kit.hpp"
#include "bootstrap/machines/common/voice-mixer.hpp"
#include "bootstrap/machines/sb97/sb97-board.hpp"
#include "contracts/hardware/floppy-activity.hpp"
#include "contracts/hardware/machine.hpp"
#include "logic/hardware/apu-2a03/apu-2a03.hpp"
#include "logic/hardware/bus/mapped-bus.hpp"
#include "logic/hardware/cartridge-slot/standard-cartridge-slot.hpp"
#include "logic/hardware/cpu-6502/cpu-6502.hpp"
#include "logic/hardware/dma-2a03/dma-2a03.hpp"
#include "logic/hardware/fdc-upd765/fdc-upd765.hpp"
#include "logic/hardware/floppy-drive/standard-floppy-drive.hpp"
#include "logic/hardware/tape-deck/tape-deck.hpp"
#include "logic/hardware/joypad/joypad.hpp"
#include "logic/hardware/keyboard-sb97/keyboard-sb97.hpp"
#include "logic/hardware/lines/line-output.hpp"
#include "logic/hardware/lines/wired-or-line.hpp"
#include "logic/hardware/pad-2a03/pad-2a03.hpp"
#include "logic/hardware/ppu-2c02/ppu-2c02.hpp"
#include "logic/hardware/ram/ram-block.hpp"
#include "logic/hardware/scheduler/master-scheduler.hpp"
#include "logic/hardware/speech-lpc/speech-lpc.hpp"

namespace ember::bootstrap {

/** 小霸王 97 学习机（Dendy 制式）。 */
class Sb97Machine final : public contracts::hardware::Machine, private contracts::hardware::MachineDebugger {
 public:
  /** 主时钟频率（赫兹）。 */
  static constexpr double MASTER_CLOCK_HZ = 26'601'712.0;
  /** 每个 CPU 周期的主时钟周期数。 */
  static constexpr contracts::hardware::MasterTime CPU_TICKS_PER_CYCLE = 15;
  /** 每个 PPU 点的主时钟周期数。 */
  static constexpr contracts::hardware::MasterTime PPU_TICKS_PER_DOT = 5;
  /** 每帧的行数与 vblank 起始行。 */
  static constexpr int SCANLINES_PER_FRAME = 312;
  static constexpr int VBLANK_LINE = 291;
  /** 软驱控制器状态机每一步的主时钟周期数（约 2.4 微秒）。 */
  static constexpr contracts::hardware::MasterTime FDC_STEP_TICKS = 64;
  /** 软驱控制器轮询一个驱动器占的步数（约 220 微秒）。 */
  static constexpr unsigned FDC_POLL_STEPS = 91;
  /** 上电与复位时写进 $4017 的值：位 7 为 5 步序列，位 6 禁止帧 IRQ。 */
  static constexpr std::uint8_t FRAME_COUNTER_RESET_VALUE = 0xC0;

  /**
   * @param bios BIOS ROM 的内容，大小必须是 1MB（不含 iNES 文件头）。
   * @param floppyActivity 软驱马达开关变化时通知它，必须活到主机销毁；为空时不通知。
   * @param floppyDrive 软驱 A：独立于主机的设备，由外面创建并在多台主机之间共用，主机创建时接入、销毁时解除，盘片留在里面；为空时主机自己带一个（测试用）。
   * @param tapeDeck 磁带机：独立于主机的设备，由外面创建并在多台主机之间共用，主机通电时接上它，机器的声音经它播放磁带、录进磁带；为空时主机自己带一台（测试用）。
   * @throws shared::CoreError hardware.invalidArgument：BIOS 大小不对。
   */
  explicit Sb97Machine(std::vector<std::uint8_t> bios, contracts::hardware::FloppyActivity* floppyActivity = nullptr, std::shared_ptr<logic::hardware::StandardFloppyDrive> floppyDrive = nullptr,
                       std::shared_ptr<logic::hardware::TapeDeck> tapeDeck = nullptr);
  ~Sb97Machine() override;

  Sb97Machine(const Sb97Machine&) = delete;
  Sb97Machine& operator=(const Sb97Machine&) = delete;

  void powerOn() override;
  void powerOff() override;
  void softReset() override;
  [[nodiscard]] bool isPoweredOn() const override { return poweredOn_; }
  void run(contracts::hardware::MasterTime ticks) override;
  void runFrame() override;
  [[nodiscard]] contracts::hardware::FrameBuffer frame() const override { return ppu_.frame(); }
  [[nodiscard]] double framesPerSecond() const override { return MASTER_CLOCK_HZ / (static_cast<double>(PPU_TICKS_PER_DOT) * 341.0 * SCANLINES_PER_FRAME); }
  [[nodiscard]] unsigned audioSampleRate() const override { return logic::hardware::Apu2a03::SAMPLE_RATE; }
  void takeAudio(std::vector<float>& samples) override;
  void setButtons(unsigned port, std::uint8_t buttons) override;
  [[nodiscard]] unsigned padPortCount() const override { return 1; }
  void input(const contracts::hardware::InputEvent& event) override;
  /** 小霸王 97 的键盘属于机器。 */
  [[nodiscard]] bool keyboardIsMachineInput() const override { return true; }
  /** 没有鼠标。 */
  [[nodiscard]] bool hasMouse() const override { return false; }
  [[nodiscard]] contracts::hardware::CpuRegisters cpuRegisters() const override { return cpu_.registers(); }
  [[nodiscard]] const contracts::hardware::Clock& clock() const override { return scheduler_; }
  contracts::hardware::CartridgeSlot& cartridgeSlot() override { return slot_; }
  contracts::hardware::IrqOutput& cartridgeIrq() override { return cartridgeIrq_; }
  contracts::hardware::MachineDebugger* debugger() override { return this; }
  [[nodiscard]] unsigned floppyDriveCount() const override { return 1; }
  contracts::hardware::FloppyDrive* floppyDrive(unsigned index) override { return index == 0 ? floppy_.get() : nullptr; }
  contracts::hardware::TapeControl* tape() override { return tapeDeck_.get(); }

  // 测试与调试用
  [[nodiscard]] logic::hardware::Ppu2c02& ppu() { return ppu_; }
  [[nodiscard]] const logic::hardware::Ppu2c02& ppu() const { return ppu_; }
  [[nodiscard]] logic::hardware::Apu2a03& apu() { return apu_; }
  [[nodiscard]] Sb97Board& board() { return board_; }
  [[nodiscard]] const Sb97Board& board() const { return board_; }
  [[nodiscard]] logic::hardware::KeyboardSb97& keyboard() { return keyboard_; }
  [[nodiscard]] logic::hardware::FdcUpd765& fdc() { return fdc_; }
  [[nodiscard]] logic::hardware::SpeechLpc& speech() { return speech_; }
  /** 2K 内部 RAM。 */
  [[nodiscard]] const contracts::hardware::StorageRegion& internalRam() const { return ram_; }
  /** 不经 CPU 读 CPU 总线，读不改变状态。 */
  std::uint8_t peekCpuBus(std::uint16_t address) { return cpuBus_.peek(address); }

 private:
  /** $4016-$4017：$4016 读手柄口 1 的数据线，$4017 读键盘；写 $4016 锁存手柄并控制键盘，写 $4017 交给 APU 的帧计数器。 */
  class InputPort final : public contracts::hardware::BusDevice {
   public:
    InputPort(contracts::hardware::ControllerPort& pad, contracts::hardware::BusDevice& apu, logic::hardware::KeyboardSb97& keyboard) : pad_(pad), apu_(apu), keyboard_(keyboard) {}
    contracts::hardware::BusRead read(std::uint16_t address) override;
    void write(std::uint16_t address, std::uint8_t value) override;

   private:
    contracts::hardware::ControllerPort& pad_;
    contracts::hardware::BusDevice& apu_;
    logic::hardware::KeyboardSb97& keyboard_;
  };

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
  /** 卡槽的 IRQ 引脚（整机机型没有卡带，只为满足 Machine 契约）。 */
  logic::hardware::LineIrqOutput cartridgeIrq_;
  /** 空卡槽：不挂载到任何总线。 */
  logic::hardware::StandardCartridgeSlot slot_;
  logic::hardware::CpuAddressBus cpuBus_;
  logic::hardware::PpuAddressBus ppuBus_;

  logic::hardware::RamBlock ram_;
  logic::hardware::MirroredRam ramDevice_;
  logic::hardware::Apu2a03 apu_;
  logic::hardware::Ppu2c02 ppu_;
  logic::hardware::Dma2a03 dma_;
  logic::hardware::Pad2a03 pad_;
  logic::hardware::Joypad joypad_;
  logic::hardware::KeyboardSb97 keyboard_;
  InputPort inputPort_;
  Sb97FdcInterrupt fdcInterrupt_;
  std::shared_ptr<logic::hardware::StandardFloppyDrive> floppy_;
  /** 磁带机：独立于主机，通电时接上。 */
  std::shared_ptr<logic::hardware::TapeDeck> tapeDeck_;
  logic::hardware::FdcUpd765 fdc_;
  logic::hardware::SpeechLpc speech_;
  VoiceMixer voiceMixer_;
  Sb97Board board_;
  logic::hardware::Cpu6502 cpu_;
  /** 断点、记录、代码数据标记与反汇编。 */
  CpuDebugKit debug_;

  bool poweredOn_{false};
};

}  // namespace ember::bootstrap
