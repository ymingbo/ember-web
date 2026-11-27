/*
 * ------------------------------------------------------------------------
 * 名称：machine-debugger.hpp
 * 说明：硬件契约：主机的调试接口：内存空间的无副作用读与写、地址解析、CPU 与芯片状态的读写、按指令或周期或扫描线或帧单步。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * 备注：只在模拟线程使用；读不改变主机的任何状态（寄存器类地址读不到内容，读出 0）；
 *       卡带上的存储区不在这里，由卡带自己提供（见 contracts/mappers/mapper-cartridge.hpp），空间名 cart.<存储区标识> 由调用方合成。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "contracts/hardware/cpu.hpp"
#include "contracts/hardware/ppu.hpp"

namespace ember::contracts::hardware {

/** 一个内存空间。 */
struct MemorySpace {
  /** 空间标识，如 cpu、ppu、board.ram、chip.oam。 */
  std::string id;
  /** 说明。 */
  std::string description;
  /** 大小（字节）；地址从 0 到大小减 1。 */
  std::size_t size{0};
  /** 能否写入。 */
  bool writable{false};
};

/** 单步的单位。 */
enum class StepUnit : std::uint8_t {
  /** 一条指令（含随后响应的中断序列）。 */
  Instruction,
  /** 至少这么多 CPU 周期；指令不被打断，所以实际周期数可能多几个。 */
  Cycle,
  /** PPU 的扫描线推进这么多行。 */
  Scanline,
  /** PPU 完成这么多帧画面。 */
  Frame
};

/** 单步的结果。 */
struct StepResult {
  /** 实际执行的指令数。 */
  std::uint64_t instructions{0};
  /** 实际经过的 CPU 周期数。 */
  std::uint64_t cycles{0};
  /** 期间完成的帧数。 */
  std::uint64_t frames{0};
};

/** 一个总线地址对应的东西。 */
struct AddressTarget {
  /** 种类：ram、rom、register（芯片或设备的寄存器）、cartridge（由卡带响应）、none（没有设备）。 */
  std::string kind;
  /** 所在的存储区或芯片的标识；没有时为空。 */
  std::string region;
  /** 在存储区里的偏移，或在寄存器组里的序号。 */
  std::size_t offset{0};
  /** 说明。 */
  std::string description;
};

/** 一项状态：名称、取值与说明。 */
struct StateField {
  std::string name;
  std::int64_t value{0};
  std::string description;
};

/** 断点的种类。 */
enum class BreakpointKind : std::uint8_t {
  /** CPU 要执行这个地址上的指令之前停下（指令还没有执行）。 */
  Execute,
  /** 总线读到这个范围里的地址，那条指令执行完后停下。 */
  Read,
  /** 总线写到这个范围里的地址，那条指令执行完后停下。 */
  Write,
  /** CPU 响应 IRQ 之后、处理程序的第一条指令执行之前停下；没有地址范围。 */
  Irq,
  /** CPU 响应 NMI 之后、处理程序的第一条指令执行之前停下；没有地址范围。 */
  Nmi,
  /** CPU 执行 BRK 进入处理程序之后、处理程序的第一条指令执行之前停下；没有地址范围。 */
  Brk,
  /** 上电或复位（软复位与硬复位）之后、第一条指令执行之前停下；没有地址范围。 */
  Reset
};

/** 断点种类的名字（协议里的写法）。 */
[[nodiscard]] inline const char* breakpointKindName(BreakpointKind kind) {
  switch (kind) {
    case BreakpointKind::Execute:
      return "execute";
    case BreakpointKind::Read:
      return "read";
    case BreakpointKind::Write:
      return "write";
    case BreakpointKind::Irq:
      return "irq";
    case BreakpointKind::Nmi:
      return "nmi";
    case BreakpointKind::Brk:
      return "brk";
    case BreakpointKind::Reset:
      return "reset";
  }
  return "";
}

/** 由名字取断点种类；名字不认识时返回空。 */
[[nodiscard]] inline std::optional<BreakpointKind> parseBreakpointKind(std::string_view name) {
  for (const BreakpointKind kind : {BreakpointKind::Execute, BreakpointKind::Read, BreakpointKind::Write, BreakpointKind::Irq, BreakpointKind::Nmi, BreakpointKind::Brk, BreakpointKind::Reset}) {
    if (name == breakpointKindName(kind)) return kind;
  }
  return std::nullopt;
}

/** 种类是否带地址范围（execute、read、write）；irq、nmi、brk、reset 没有。 */
[[nodiscard]] inline bool breakpointHasAddress(BreakpointKind kind) { return kind == BreakpointKind::Execute || kind == BreakpointKind::Read || kind == BreakpointKind::Write; }

/** 一个断点。 */
struct Breakpoint {
  std::uint32_t id{0};
  BreakpointKind kind{BreakpointKind::Execute};
  /** 地址范围的第一个与最后一个地址（含）；irq、nmi、brk、reset 没有地址范围，都为 0。 */
  std::uint16_t first{0};
  std::uint16_t last{0};
  /** 已经命中的次数（只算条件成立的命中）。 */
  std::uint64_t hits{0};
  /** 是否启用；禁用的断点保留在表里，不触发。 */
  bool enabled{true};
  /** 条件表达式（语法见 docs/04 第 4.5 节）；命中的地址与条件都满足才算命中；空表示没有条件。 */
  std::string condition;
};

/** 一次断点命中。 */
struct BreakpointHit {
  std::uint32_t id{0};
  BreakpointKind kind{BreakpointKind::Execute};
  /** 执行断点是将要执行的指令地址；读写断点是被访问的地址；irq、nmi、brk 是处理程序的地址；reset 是复位后的 PC。 */
  std::uint16_t address{0};
  /** 读写断点读到或写入的值；其余为 0。 */
  std::uint8_t value{0};
};

/** 记录的种类。 */
enum class RecorderKind : std::uint8_t {
  /** 每执行一条指令记一行：地址、字节、反汇编与寄存器。 */
  Instruction,
  /** 每次访问芯片寄存器、手柄口、卡带的扩展寄存器（$2000-$5FFF），以及对 $8000 以上的写（mapper 寄存器）记一行。 */
  Io,
  /** 每次 CPU 响应 IRQ、NMI 或 BRK 记一行。 */
  Irq,
  /** 每次执行到指定地址记一行：寄存器、栈顶与内存段。 */
  Call
};

/** 一段内存。 */
struct MemoryRange {
  std::uint16_t address{0};
  std::uint16_t length{0};
};

/** 一个字节的取值条件。 */
struct ByteCondition {
  std::uint16_t address{0};
  std::uint8_t value{0};
};

/** 开始一项记录的参数。 */
struct RecorderSpec {
  RecorderKind kind{RecorderKind::Instruction};
  /** Call：要记录的地址。 */
  std::uint16_t address{0};
  /** Call：每次记录栈顶的字节数。 */
  std::uint8_t stackBytes{0};
  /** Call：每次记录的内存段，CPU 总线上的地址。 */
  std::vector<MemoryRange> ranges;
  /** Call：任何一项成立（这个地址上的字节等于这个值）就不记录这一次。 */
  std::vector<ByteCondition> skip;
  /** 最多记录的行数。 */
  std::uint32_t maxLines{100000};
  /** Instruction：每个地址只记第一次。 */
  bool unique{false};
  /** 环形：行数满了以后丢弃最早的行继续记录（可以边记边读，见 readRecorder）；为 false 时满了就不再记录。 */
  bool ring{false};
};

/** 一项记录的结果。 */
struct RecorderOutput {
  std::vector<std::string> lines;
  /** 因为行数满了而少记了（非环形）。 */
  bool truncated{false};
  /** 环形记录里被丢弃的最早的行数。 */
  std::uint64_t dropped{0};
};

/** 一项正在进行的记录的一段内容（不结束记录）。 */
struct RecorderSlice {
  /** 取到的行。 */
  std::vector<std::string> lines;
  /** lines[0] 的行号（从 0 起，记录开始以来的绝对行号）。 */
  std::uint64_t first{0};
  /** 记录开始以来已经产生的行数；下一行的行号。 */
  std::uint64_t total{0};
  /** 还留着的最早一行的行号（环形记录丢弃最早的行后大于 0）。 */
  std::uint64_t oldest{0};
  /** 因为行数满了而少记了（非环形）。 */
  bool truncated{false};
};

/** 代码与数据标记（CDL）的标记位，每个 PRG-ROM 字节一个标记字节，位定义同 Mesen。 */
/** 被当作指令执行（含操作数字节）。 */
inline constexpr std::uint8_t CDL_CODE = 0x01;
/** 被指令当数据读取。 */
inline constexpr std::uint8_t CDL_DATA = 0x02;
/** 是 JMP 或成立的分支的目标。 */
inline constexpr std::uint8_t CDL_JUMP_TARGET = 0x04;
/** 是 JSR 的目标（子程序入口）。 */
inline constexpr std::uint8_t CDL_SUB_ENTRY = 0x08;

/** CPU 地址换算成 PRG-ROM 的偏移（按当前的分页）；不在 PRG-ROM 里时返回空。 */
using CdlResolver = std::function<std::optional<std::size_t>(std::uint16_t)>;

/** 一条反汇编的指令。 */
struct DisassembledInstruction {
  std::uint16_t address{0};
  /** 指令字节，十六进制，空格隔开，如 "A9 42"。 */
  std::string bytes;
  /** 助记符与操作数，带有效地址与取值的注释，如 "LDA $0180 = 33"。 */
  std::string text;
  /** 是不是非官方指令。 */
  bool unofficial{false};
};

/** 调用栈帧的种类。 */
enum class CallKind : std::uint8_t {
  /** JSR 调用的子程序。 */
  Jsr,
  /** 响应 IRQ 进入的处理程序。 */
  Irq,
  /** 响应 NMI 进入的处理程序。 */
  Nmi,
  /** BRK 指令进入的处理程序。 */
  Brk,
  /** 不在任何子程序与处理程序里的代码；只在统计里出现，调用栈里没有。 */
  Root
};

/** 调用栈的一帧。 */
struct CallFrame {
  CallKind kind{CallKind::Jsr};
  /** 发起调用的位置：JSR 与 BRK 是指令本身的地址，IRQ 与 NMI 是被打断的、还没有执行的那条指令的地址。 */
  std::uint16_t callAddress{0};
  /** 子程序或处理程序的入口。 */
  std::uint16_t targetAddress{0};
  /** 返回后执行的地址（JSR 是它后面那条指令，BRK 是 BRK 加 2，IRQ 与 NMI 是被打断的那条指令）。 */
  std::uint16_t returnAddress{0};
  /** 调用完成后的栈指针（返回地址压在它之上）。 */
  std::uint8_t stackPointer{0};
};

/** 子程序统计里的一项。 */
struct ProfileEntry {
  CallKind kind{CallKind::Jsr};
  /** 入口地址；kind 为 Root 时为 0。 */
  std::uint16_t address{0};
  /** 调用次数（完成的调用，加上统计停下时还没有返回的那一次）；Root 为 1。 */
  std::uint64_t calls{0};
  /** 含子程序的 CPU 周期数；递归时里层的周期被重复计入。 */
  std::uint64_t inclusiveCycles{0};
  /** 不含被它调用的子程序与中断处理程序的 CPU 周期数。 */
  std::uint64_t exclusiveCycles{0};
};

/** 子程序统计的结果。 */
struct ProfileReport {
  /** 统计是否还在进行。 */
  bool active{false};
  /** 统计期间经过的 CPU 周期数。 */
  std::uint64_t totalCycles{0};
  /** 各子程序与处理程序，按不含子程序的周期数从大到小排列；Root 一项在里面。 */
  std::vector<ProfileEntry> entries;
};

/** 事件的种类。 */
enum class EventKind : std::uint8_t {
  /** 读 $2000-$5FFF 里的地址。 */
  Read,
  /** 写 $2000-$5FFF 里的地址，或写 $8000 以上的地址（mapper 寄存器）。 */
  Write,
  /** CPU 响应了 IRQ。 */
  Irq,
  /** CPU 响应了 NMI。 */
  Nmi,
  /** CPU 执行了 BRK。 */
  Brk
};

/** 一个事件。 */
struct DebugEvent {
  /** 帧序号（见 VideoPosition）。 */
  std::uint64_t frame{0};
  std::int32_t scanline{0};
  std::int32_t dot{0};
  EventKind kind{EventKind::Read};
  /** Read、Write 是被访问的地址；Irq、Nmi、Brk 是处理程序的地址。 */
  std::uint16_t address{0};
  /** Read、Write 读到或写入的值；其余为 0。 */
  std::uint8_t value{0};
  /** Read、Write 是发起访问的那条指令的地址；Irq、Nmi、Brk 是返回地址。 */
  std::uint16_t pc{0};
  /** 事件发生时的 CPU 周期数。 */
  std::uint64_t cycles{0};
};

/** 事件记录的内容。 */
struct EventLogOutput {
  /** 记录是否还在进行。 */
  bool active{false};
  /** 现在的帧序号。 */
  std::uint64_t frame{0};
  /** 因为容量满了而被丢弃的最早的事件数（从记录开始累计）。 */
  std::uint64_t dropped{0};
  /** 事件，按发生的先后。 */
  std::vector<DebugEvent> events;
};

/** 主机的调试接口。 */
class MachineDebugger {
 public:
  virtual ~MachineDebugger() = default;

  /** 主机自己拥有的内存空间（不含卡带上的存储区）。 */
  [[nodiscard]] virtual std::vector<MemorySpace> memorySpaces() const = 0;

  /**
   * 无副作用地读一段内存。
   * @throws shared::CoreError hardware.unknownSpace、hardware.spaceOutOfRange。
   */
  virtual void readMemory(std::string_view space, std::uint32_t address, std::span<std::uint8_t> out) = 0;

  /**
   * 写一段内存。cpu 空间的写经真实的总线，与 CPU 写一样有副作用。
   * @throws shared::CoreError hardware.unknownSpace、hardware.spaceOutOfRange、hardware.spaceReadOnly。
   */
  virtual void writeMemory(std::string_view space, std::uint32_t address, std::span<const std::uint8_t> bytes) = 0;

  /**
   * 解析总线地址。
   * @param space cpu 或 ppu。
   * @throws shared::CoreError hardware.unknownSpace、hardware.spaceOutOfRange。
   */
  [[nodiscard]] virtual AddressTarget resolve(std::string_view space, std::uint32_t address) const = 0;

  /** 可以读写状态的模块标识，如 cpu、ppu。 */
  [[nodiscard]] virtual std::vector<std::string> stateModules() const = 0;

  /**
   * 读一个模块的状态。
   * @throws shared::CoreError hardware.invalidState：没有这个模块。
   */
  [[nodiscard]] virtual std::vector<StateField> readState(std::string_view module) const = 0;

  /**
   * 修改一个模块的状态；先全部检查，有任何一项不合格就一项也不改。
   * @throws shared::CoreError hardware.invalidState：模块不存在、状态项不存在或只读、取值超出范围。
   */
  virtual void writeState(std::string_view module, std::span<const StateField> fields) = 0;

  /**
   * 单步，或按授权量运行（runtime.run 的 cycles、instructions、scanlines）。
   * @param unit 单位。
   * @param count 数量，至少 1。
   * @param stopAtBreakpoints 为 false（单步）时忽略断点；为 true 时断点照常生效，命中后提前返回，命中用 takeBreakpointHit 取走，继续时越过刚命中的执行断点。
   */
  virtual StepResult step(StepUnit unit, std::uint64_t count, bool stopAtBreakpoints = false) = 0;

  /**
   * 添加断点。断点在主机的 runFrame 里生效（自由运行与授权运行）；单步（step）忽略断点。
   * @param first 地址范围的第一个地址。
   * @param last 地址范围的最后一个地址（含），不小于 first。
   * @param condition 条件表达式，空表示没有条件。
   * @throws shared::CoreError hardware.invalidBreakpoint、hardware.breakpointLimit、hardware.invalidCondition。
   */
  virtual Breakpoint addBreakpoint(BreakpointKind kind, std::uint16_t first, std::uint16_t last, const std::string& condition) = 0;

  /**
   * 修改断点的启用状态或条件；没有给出的项不变；没有这个编号时返回空。
   * @param condition 新的条件表达式，空字符串表示去掉条件。
   * @throws shared::CoreError hardware.invalidCondition。
   */
  virtual std::optional<Breakpoint> updateBreakpoint(std::uint32_t id, std::optional<bool> enabled, const std::optional<std::string>& condition) = 0;

  /** 删除断点；没有这个编号时返回 false。 */
  virtual bool removeBreakpoint(std::uint32_t id) = 0;

  /** 删除全部断点，返回删除的个数。 */
  virtual std::size_t clearBreakpoints() = 0;

  /** 全部断点，按编号升序。 */
  [[nodiscard]] virtual std::vector<Breakpoint> breakpoints() const = 0;

  /** 取走最近一次运行停下的断点命中；没有命中时为空。 */
  virtual std::optional<BreakpointHit> takeBreakpointHit() = 0;

  /**
   * 开始一项记录，之后运行与单步产生的内容记进内存，直到 stopRecorder 取走。记录属于当前的主机。
   * @return 记录编号。
   * @throws shared::CoreError hardware.recorderLimit。
   */
  virtual std::uint32_t startRecorder(const RecorderSpec& spec) = 0;

  /**
   * 结束一项记录并取走内容。
   * @throws shared::CoreError hardware.noRecorder。
   */
  virtual RecorderOutput stopRecorder(std::uint32_t id) = 0;

  /**
   * 读一项正在进行的记录的一段内容，不结束记录。
   * @param from 要读的第一行的行号（记录开始以来的绝对行号）；早于还留着的最早一行时从最早一行读起。
   * @param maxLines 最多读的行数。
   * @throws shared::CoreError hardware.noRecorder。
   */
  [[nodiscard]] virtual RecorderSlice readRecorder(std::uint32_t id, std::uint64_t from, std::uint32_t maxLines) const = 0;

  /**
   * 从 address 起反汇编 count 条指令（CPU 总线上的地址，无副作用地读内存，芯片寄存器类地址的取值显示 00；带索引的寻址用当前的 X、Y）。
   */
  [[nodiscard]] virtual std::vector<DisassembledInstruction> disassemble(std::uint16_t address, std::uint32_t count) = 0;

  /**
   * 开始代码与数据标记：标记清零，之后运行与单步执行的每条指令都打标记。
   * @param size PRG-ROM 的字节数。
   * @throws shared::CoreError hardware.cdlActive。
   */
  virtual void startCdl(std::size_t size, CdlResolver resolver) = 0;

  /** 停止标记，已有的标记保留到下一次 startCdl 或换主机。 */
  virtual void stopCdl() = 0;

  /** 标记（每个 PRG 字节一个标记字节）；没有记录过时为空。 */
  [[nodiscard]] virtual std::optional<std::vector<std::uint8_t>> cdlFlags() const = 0;

  /** 调用栈，最里层的在前；记录从主机上电（或复位）起一直在进行。 */
  [[nodiscard]] virtual std::vector<CallFrame> callStack() const = 0;

  /** 开始子程序统计：统计清零，之后经过的周期按子程序累计；已经在统计时重新开始。 */
  virtual void startProfiler() = 0;

  /** 停止统计，结果保留到下一次 startProfiler 或换主机。 */
  virtual void stopProfiler() = 0;

  /** 子程序统计的当前结果；没有开始过时为空。 */
  [[nodiscard]] virtual std::optional<ProfileReport> profile() const = 0;

  /**
   * 开始事件记录：记录清零，之后运行与单步产生的事件记在内存里，容量满了丢弃最早的；已经在记录时重新开始。
   * @param maxEvents 最多保留的事件数。
   */
  virtual void startEventLog(std::uint32_t maxEvents) = 0;

  /** 停止事件记录，已有的事件保留到下一次 startEventLog 或换主机。 */
  virtual void stopEventLog() = 0;

  /**
   * 事件记录的内容；没有开始过时为空。
   * @param fromFrame 只取这个帧序号及以后的事件；为空时取全部。
   * @param maxEvents 最多取的事件数；超过时取最新的。
   */
  [[nodiscard]] virtual std::optional<EventLogOutput> eventLog(std::optional<std::uint64_t> fromFrame, std::uint32_t maxEvents) const = 0;

  /** 64 个颜色号现在显示的颜色，每项内存顺序 R、G、B、A（小端整数为 0xAABBGGRR），已按当前的颜色强调与灰度位换算。 */
  [[nodiscard]] virtual std::vector<std::uint32_t> colorTable() const = 0;
};

}  // namespace ember::contracts::hardware
