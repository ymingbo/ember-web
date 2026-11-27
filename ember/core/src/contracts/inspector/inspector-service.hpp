/*
 * ------------------------------------------------------------------------
 * 名称：inspector-service.hpp
 * 说明：inspector 契约：检查器服务：列出内存空间、读写内存、解析总线地址、读写模块状态。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * 备注：可在任意线程调用，实际访问由模拟线程完成；错误码见 inspector-error.hpp。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "contracts/hardware/machine-debugger.hpp"

namespace ember::contracts::inspector {

/** 一个模块的状态。 */
struct StateReport {
  std::string module;
  bool writable{false};
  std::vector<hardware::StateField> fields;
};

/** 一个监视中的状态项。 */
struct WatchEntry {
  /** 状态路径：<模块>.<状态项>。 */
  std::string path;
  std::int64_t value{0};
};

/** 一个时刻的完整快照。 */
struct SnapshotReport {
  /** 自上电以来的 CPU 周期数。 */
  std::uint64_t cycles{0};
  std::vector<StateReport> states;
  std::vector<hardware::Breakpoint> breakpoints;
  std::vector<WatchEntry> watches;
};

/** 一项记录的结果。 */
struct RecordingReport {
  /** 保存的文件的完整路径。 */
  std::string file;
  /** 写入的行数。 */
  std::size_t lines{0};
  /** 因为行数满了而少记了。 */
  bool truncated{false};
};

/** 代码与数据标记的统计。 */
struct CdlReport {
  /** 保存的文件的完整路径；没有保存时为空。 */
  std::string file;
  /** PRG-ROM 的字节数。 */
  std::size_t size{0};
  std::size_t codeBytes{0};
  std::size_t dataBytes{0};
};

/** 检查器服务。 */
class InspectorService {
 public:
  virtual ~InspectorService() = default;

  /** 全部内存空间：主机自己的，加上卡带上的存储区（cart.<标识>）。 */
  virtual std::vector<hardware::MemorySpace> listSpaces() = 0;

  /** 无副作用地读一段内存。 */
  virtual std::vector<std::uint8_t> readMemory(const std::string& space, std::uint32_t address, std::uint32_t length) = 0;

  /** 写一段内存；返回写入的字节数。 */
  virtual std::size_t writeMemory(const std::string& space, std::uint32_t address, std::span<const std::uint8_t> bytes) = 0;

  /** 解析总线地址（space 为 cpu 或 ppu）。 */
  virtual hardware::AddressTarget resolve(const std::string& space, std::uint32_t address) = 0;

  /** 读状态；module 为空时返回全部模块。 */
  virtual std::vector<StateReport> getState(const std::optional<std::string>& module) = 0;

  /** 修改一个模块的状态，返回修改之后的状态。 */
  virtual StateReport setState(const std::string& module, std::span<const hardware::StateField> fields) = 0;

  /** 完整快照；module 为空时包含全部模块的状态。 */
  virtual SnapshotReport snapshot(const std::optional<std::string>& module) = 0;

  /** 监视一个状态项（取值变化时发 inspector.stateChanged），返回当前取值；已在监视时没有效果。 */
  virtual WatchEntry watch(const std::string& path) = 0;

  /** 取消监视；没有监视这个路径时抛出 inspector.noWatch。 */
  virtual void unwatch(const std::string& path) = 0;

  /** 从 address 起反汇编 count 条指令。 */
  virtual std::vector<hardware::DisassembledInstruction> disassemble(std::uint16_t address, std::uint32_t count) = 0;

  /**
   * 开始代码与数据标记，返回 PRG-ROM 的字节数（CdlReport::size）。
   * @throws shared::CoreError inspector.unsupported（卡带没有 PRG-ROM 存储区）、inspector.cdlActive。
   */
  virtual CdlReport startCdl() = 0;

  /** 停止标记并返回统计。@throws shared::CoreError inspector.noCdl。 */
  virtual CdlReport stopCdl() = 0;

  /**
   * 把标记写成调试输出目录下的文件；fileName 为空时用 prg.cdl。
   * @throws shared::CoreError inspector.noCdl、inspector.invalidFileName、inspector.fileFailed。
   */
  virtual CdlReport saveCdl(const std::string& fileName) = 0;

  /**
   * 开始跟踪或调用记录：内容记在内存里，stopRecording 时写成调试输出目录下的文件。
   * @param fileName 文件名；为空时按种类与编号生成。
   * @return 记录编号。
   * @throws shared::CoreError output.invalidFileName、inspector.recordingLimit。
   */
  virtual std::uint32_t startRecording(const hardware::RecorderSpec& spec, const std::string& fileName) = 0;

  /**
   * 结束记录并写文件。callLog 为 true 时只能结束调用记录，否则只能结束跟踪。
   * @throws shared::CoreError inspector.noRecording、output.fileFailed。
   */
  virtual RecordingReport stopRecording(std::uint32_t id, bool callLog) = 0;

  /**
   * 读一项正在进行的记录（跟踪或调用记录）的一段内容，不结束记录。
   * @param from 要读的第一行的行号。
   * @throws shared::CoreError inspector.noRecording。
   */
  virtual hardware::RecorderSlice readRecording(std::uint32_t id, std::uint64_t from, std::uint32_t maxLines) = 0;

  /**
   * 添加断点（CPU 总线上的地址，范围含两端）。
   * @param condition 条件表达式，空表示没有条件。
   * @throws shared::CoreError inspector.invalidBreakpoint、inspector.breakpointLimit、inspector.invalidCondition。
   */
  virtual hardware::Breakpoint addBreakpoint(hardware::BreakpointKind kind, std::uint16_t first, std::uint16_t last, const std::string& condition) = 0;

  /**
   * 修改断点的启用状态或条件，没有给出的项不变。
   * @throws shared::CoreError inspector.noBreakpoint、inspector.invalidCondition。
   */
  virtual hardware::Breakpoint updateBreakpoint(std::uint32_t id, std::optional<bool> enabled, const std::optional<std::string>& condition) = 0;

  /** 全部断点。 */
  virtual std::vector<hardware::Breakpoint> listBreakpoints() = 0;

  /** 删除一个断点；没有这个编号时抛出 inspector.noBreakpoint。 */
  virtual void removeBreakpoint(std::uint32_t id) = 0;

  /** 删除全部断点，返回删除的个数。 */
  virtual std::size_t clearBreakpoints() = 0;

  /** 调用栈，最里层的在前。 */
  virtual std::vector<hardware::CallFrame> callStack() = 0;

  /** 开始子程序统计（统计清零，已经在统计时重新开始）。 */
  virtual void startProfiler() = 0;

  /** 停止子程序统计并返回结果。@throws shared::CoreError inspector.noProfile。 */
  virtual hardware::ProfileReport stopProfiler() = 0;

  /** 子程序统计的当前结果，不停止统计。@throws shared::CoreError inspector.noProfile。 */
  virtual hardware::ProfileReport profile() = 0;

  /** 开始事件记录（已经在记录时重新开始）。 */
  virtual void startEventLog(std::uint32_t maxEvents) = 0;

  /** 停止事件记录。@throws shared::CoreError inspector.noEventLog。 */
  virtual void stopEventLog() = 0;

  /** 读事件记录的内容。@throws shared::CoreError inspector.noEventLog。 */
  virtual hardware::EventLogOutput eventLog(std::optional<std::uint64_t> fromFrame, std::uint32_t maxEvents) = 0;

  /** 64 个颜色号现在显示的颜色，每项 4 个字节 R、G、B、A。 */
  virtual std::vector<std::uint8_t> palette() = 0;
};

}  // namespace ember::contracts::inspector
