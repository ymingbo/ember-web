/*
 * ------------------------------------------------------------------------
 * 名称：inspector-requests.cpp
 * 说明：inspector 模块的请求登记，见 inspector-requests.hpp。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * 备注：readMemory 的 data 以 nlohmann::json::binary 放在结果里，调试服务据此改发二进制帧。
 * ------------------------------------------------------------------------
 */

#include "requests/inspector/inspector-requests.hpp"

#include <utility>

#include "contracts/inspector/generated/inspector.hpp"
#include "contracts/inspector/inspector-error.hpp"
#include "shared/errors/core-error.hpp"

namespace ember::requests::inspector {

namespace contract = ::ember::contracts::inspector;

namespace {

namespace hw = ::ember::contracts::hardware;

/** 记录的默认行数上限，与 Schema 里的说明一致。 */
constexpr std::int64_t DEFAULT_RECORDER_LINES = 100000;

/** 反汇编的默认条数，与 Schema 里的说明一致。 */
constexpr std::int64_t DEFAULT_DISASSEMBLE_COUNT = 16;

/** 事件记录的默认容量与默认读取个数，记录读取的默认行数，与 Schema 里的说明一致。 */
constexpr std::int64_t DEFAULT_EVENT_CAPACITY = 65536;
constexpr std::int64_t DEFAULT_EVENT_READ = 20000;
constexpr std::int64_t DEFAULT_TRACE_READ_LINES = 1000;

contract::Breakpoint breakpointToWire(const hw::Breakpoint& breakpoint) {
  contract::Breakpoint wire;
  wire.id = breakpoint.id;
  wire.kind = hw::breakpointKindName(breakpoint.kind);
  wire.address = breakpoint.first;
  wire.endAddress = breakpoint.last;
  wire.hits = static_cast<std::int64_t>(breakpoint.hits);
  wire.enabled = breakpoint.enabled;
  if (!breakpoint.condition.empty()) wire.condition = breakpoint.condition;
  return wire;
}

std::string callKindName(hw::CallKind kind) {
  switch (kind) {
    case hw::CallKind::Jsr:
      return "jsr";
    case hw::CallKind::Irq:
      return "irq";
    case hw::CallKind::Nmi:
      return "nmi";
    case hw::CallKind::Brk:
      return "brk";
    case hw::CallKind::Root:
      return "root";
  }
  return "";
}

std::string eventKindName(hw::EventKind kind) {
  switch (kind) {
    case hw::EventKind::Read:
      return "read";
    case hw::EventKind::Write:
      return "write";
    case hw::EventKind::Irq:
      return "irq";
    case hw::EventKind::Nmi:
      return "nmi";
    case hw::EventKind::Brk:
      return "brk";
  }
  return "";
}

std::vector<contract::ProfileEntry> profileEntriesToWire(const hw::ProfileReport& report) {
  std::vector<contract::ProfileEntry> entries;
  entries.reserve(report.entries.size());
  for (const auto& entry : report.entries) {
    entries.push_back({callKindName(entry.kind), entry.address, static_cast<std::int64_t>(entry.calls), static_cast<std::int64_t>(entry.inclusiveCycles), static_cast<std::int64_t>(entry.exclusiveCycles)});
  }
  return entries;
}

contract::ModuleState toWire(const contract::StateReport& state) {
  contract::ModuleState wire;
  wire.module = state.module;
  wire.writable = state.writable;
  for (const auto& field : state.fields) wire.fields.push_back({field.name, field.value, field.description});
  return wire;
}

}  // namespace

void addInspectorRequests(platform::RequestRouter& router, std::shared_ptr<contract::InspectorService> service) {
  router.add("inspector", contract::INSPECTOR_LIST_SPACES_REQUEST, [service](const nlohmann::json& params, const platform::CancellationToken&) {
    static_cast<void>(contract::parseInspectorListSpacesParams(params, "params"));
    contract::InspectorListSpacesResult result;
    for (const auto& space : service->listSpaces()) result.spaces.push_back({space.id, space.description, static_cast<std::int64_t>(space.size), space.writable});
    return nlohmann::json(result);
  });
  router.add("inspector", contract::INSPECTOR_READ_MEMORY_REQUEST, [service](const nlohmann::json& params, const platform::CancellationToken&) {
    const auto parsed = contract::parseInspectorReadMemoryParams(params, "params");
    auto bytes = service->readMemory(parsed.space, static_cast<std::uint32_t>(parsed.address), static_cast<std::uint32_t>(parsed.length));
    contract::InspectorReadMemoryResult result;
    result.space = parsed.space;
    result.address = parsed.address;
    result.length = parsed.length;
    nlohmann::json json(result);
    json["data"] = nlohmann::json::binary(std::move(bytes));
    return json;
  });
  router.add("inspector", contract::INSPECTOR_WRITE_MEMORY_REQUEST, [service](const nlohmann::json& params, const platform::CancellationToken&) {
    const auto parsed = contract::parseInspectorWriteMemoryParams(params, "params");
    std::vector<std::uint8_t> bytes;
    bytes.reserve(parsed.bytes.size());
    for (const auto value : parsed.bytes) bytes.push_back(static_cast<std::uint8_t>(value));
    const auto written = service->writeMemory(parsed.space, static_cast<std::uint32_t>(parsed.address), bytes);
    return nlohmann::json(contract::InspectorWriteMemoryResult{parsed.space, parsed.address, static_cast<std::int64_t>(written)});
  });
  router.add("inspector", contract::INSPECTOR_RESOLVE_REQUEST, [service](const nlohmann::json& params, const platform::CancellationToken&) {
    const auto parsed = contract::parseInspectorResolveParams(params, "params");
    const auto target = service->resolve(parsed.space, static_cast<std::uint32_t>(parsed.address));
    contract::AddressInfo wire;
    wire.kind = target.kind;
    if (!target.region.empty()) {
      wire.region = target.region;
      wire.offset = static_cast<std::int64_t>(target.offset);
    }
    wire.description = target.description;
    return nlohmann::json(contract::InspectorResolveResult{wire});
  });
  router.add("inspector", contract::INSPECTOR_GET_STATE_REQUEST, [service](const nlohmann::json& params, const platform::CancellationToken&) {
    const auto parsed = contract::parseInspectorGetStateParams(params, "params");
    contract::InspectorGetStateResult result;
    for (const auto& state : service->getState(parsed.module)) result.states.push_back(toWire(state));
    return nlohmann::json(result);
  });
  router.add("inspector", contract::INSPECTOR_SNAPSHOT_REQUEST, [service](const nlohmann::json& params, const platform::CancellationToken&) {
    const auto parsed = contract::parseInspectorSnapshotParams(params, "params");
    const auto report = service->snapshot(parsed.module);
    contract::InspectorSnapshotResult result;
    result.cycles = static_cast<std::int64_t>(report.cycles);
    for (const auto& state : report.states) result.states.push_back(toWire(state));
    for (const auto& breakpoint : report.breakpoints) result.breakpoints.push_back(breakpointToWire(breakpoint));
    for (const auto& watch : report.watches) result.watches.push_back({watch.path, watch.value});
    return nlohmann::json(result);
  });
  router.add("inspector", contract::INSPECTOR_WATCH_REQUEST, [service](const nlohmann::json& params, const platform::CancellationToken&) {
    const auto parsed = contract::parseInspectorWatchParams(params, "params");
    const auto watch = service->watch(parsed.path);
    return nlohmann::json(contract::InspectorWatchResult{{watch.path, watch.value}});
  });
  router.add("inspector", contract::INSPECTOR_UNWATCH_REQUEST, [service](const nlohmann::json& params, const platform::CancellationToken&) {
    const auto parsed = contract::parseInspectorUnwatchParams(params, "params");
    service->unwatch(parsed.path);
    return nlohmann::json(contract::InspectorUnwatchResult{});
  });
  router.add("inspector", contract::INSPECTOR_DISASSEMBLE_REQUEST, [service](const nlohmann::json& params, const platform::CancellationToken&) {
    const auto parsed = contract::parseInspectorDisassembleParams(params, "params");
    contract::InspectorDisassembleResult result;
    for (const auto& line : service->disassemble(static_cast<std::uint16_t>(parsed.address), static_cast<std::uint32_t>(parsed.count.value_or(DEFAULT_DISASSEMBLE_COUNT)))) {
      contract::DisassembledInstruction wire;
      wire.address = line.address;
      wire.bytes = line.bytes;
      wire.text = line.text;
      wire.unofficial = line.unofficial;
      result.instructions.push_back(std::move(wire));
    }
    return nlohmann::json(result);
  });
  router.add("inspector", contract::INSPECTOR_CDL_START_REQUEST, [service](const nlohmann::json& params, const platform::CancellationToken&) {
    static_cast<void>(contract::parseInspectorCdlStartParams(params, "params"));
    return nlohmann::json(contract::InspectorCdlStartResult{static_cast<std::int64_t>(service->startCdl().size)});
  });
  router.add("inspector", contract::INSPECTOR_CDL_STOP_REQUEST, [service](const nlohmann::json& params, const platform::CancellationToken&) {
    static_cast<void>(contract::parseInspectorCdlStopParams(params, "params"));
    const auto report = service->stopCdl();
    return nlohmann::json(contract::InspectorCdlStopResult{static_cast<std::int64_t>(report.size), static_cast<std::int64_t>(report.codeBytes), static_cast<std::int64_t>(report.dataBytes)});
  });
  router.add("inspector", contract::INSPECTOR_CDL_SAVE_REQUEST, [service](const nlohmann::json& params, const platform::CancellationToken&) {
    const auto parsed = contract::parseInspectorCdlSaveParams(params, "params");
    const auto report = service->saveCdl(parsed.file.value_or(""));
    return nlohmann::json(contract::InspectorCdlSaveResult{report.file, static_cast<std::int64_t>(report.size), static_cast<std::int64_t>(report.codeBytes), static_cast<std::int64_t>(report.dataBytes)});
  });
  router.add("inspector", contract::INSPECTOR_TRACE_START_REQUEST, [service](const nlohmann::json& params, const platform::CancellationToken&) {
    const auto parsed = contract::parseInspectorTraceStartParams(params, "params");
    hw::RecorderSpec spec;
    spec.kind = parsed.kind == "io" ? hw::RecorderKind::Io : (parsed.kind == "irq" ? hw::RecorderKind::Irq : hw::RecorderKind::Instruction);
    spec.maxLines = static_cast<std::uint32_t>(parsed.maxLines.value_or(DEFAULT_RECORDER_LINES));
    spec.unique = parsed.unique.value_or(false);
    spec.ring = parsed.ring.value_or(false);
    return nlohmann::json(contract::InspectorTraceStartResult{static_cast<std::int64_t>(service->startRecording(spec, parsed.file.value_or("")))});
  });
  router.add("inspector", contract::INSPECTOR_TRACE_STOP_REQUEST, [service](const nlohmann::json& params, const platform::CancellationToken&) {
    const auto parsed = contract::parseInspectorTraceStopParams(params, "params");
    const auto report = service->stopRecording(static_cast<std::uint32_t>(parsed.id), false);
    return nlohmann::json(contract::InspectorTraceStopResult{report.file, static_cast<std::int64_t>(report.lines), report.truncated});
  });
  router.add("inspector", contract::INSPECTOR_CALL_LOG_START_REQUEST, [service](const nlohmann::json& params, const platform::CancellationToken&) {
    const auto parsed = contract::parseInspectorCallLogStartParams(params, "params");
    hw::RecorderSpec spec;
    spec.kind = hw::RecorderKind::Call;
    spec.address = static_cast<std::uint16_t>(parsed.address);
    spec.stackBytes = static_cast<std::uint8_t>(parsed.stackBytes.value_or(0));
    for (const auto& range : parsed.ranges.value_or(std::vector<contract::MemoryRangeSpec>{})) spec.ranges.push_back({static_cast<std::uint16_t>(range.address), static_cast<std::uint16_t>(range.length)});
    for (const auto& condition : parsed.skip.value_or(std::vector<contract::ByteCondition>{})) spec.skip.push_back({static_cast<std::uint16_t>(condition.address), static_cast<std::uint8_t>(condition.value)});
    spec.maxLines = static_cast<std::uint32_t>(parsed.maxLines.value_or(DEFAULT_RECORDER_LINES));
    return nlohmann::json(contract::InspectorCallLogStartResult{static_cast<std::int64_t>(service->startRecording(spec, parsed.file.value_or("")))});
  });
  router.add("inspector", contract::INSPECTOR_CALL_LOG_STOP_REQUEST, [service](const nlohmann::json& params, const platform::CancellationToken&) {
    const auto parsed = contract::parseInspectorCallLogStopParams(params, "params");
    const auto report = service->stopRecording(static_cast<std::uint32_t>(parsed.id), true);
    return nlohmann::json(contract::InspectorCallLogStopResult{report.file, static_cast<std::int64_t>(report.lines), report.truncated});
  });
  router.add("inspector", contract::INSPECTOR_ADD_BREAKPOINT_REQUEST, [service](const nlohmann::json& params, const platform::CancellationToken&) {
    const auto parsed = contract::parseInspectorAddBreakpointParams(params, "params");
    // Schema 已限定种类的取值，这里一定认得。
    const hw::BreakpointKind kind = hw::parseBreakpointKind(parsed.kind).value_or(hw::BreakpointKind::Execute);
    if (hw::breakpointHasAddress(kind) && !parsed.address.has_value()) {
      throw shared::CoreError(contract::ERROR_INVALID_BREAKPOINT, "此种断点要给 address");
    }
    const auto first = static_cast<std::uint16_t>(parsed.address.value_or(0));
    const auto last = static_cast<std::uint16_t>(parsed.endAddress.value_or(parsed.address.value_or(0)));
    return nlohmann::json(contract::InspectorAddBreakpointResult{breakpointToWire(service->addBreakpoint(kind, first, last, parsed.condition.value_or("")))});
  });
  router.add("inspector", contract::INSPECTOR_UPDATE_BREAKPOINT_REQUEST, [service](const nlohmann::json& params, const platform::CancellationToken&) {
    const auto parsed = contract::parseInspectorUpdateBreakpointParams(params, "params");
    return nlohmann::json(contract::InspectorUpdateBreakpointResult{breakpointToWire(service->updateBreakpoint(static_cast<std::uint32_t>(parsed.id), parsed.enabled, parsed.condition))});
  });
  router.add("inspector", contract::INSPECTOR_CALL_STACK_REQUEST, [service](const nlohmann::json& params, const platform::CancellationToken&) {
    static_cast<void>(contract::parseInspectorCallStackParams(params, "params"));
    contract::InspectorCallStackResult result;
    for (const auto& frame : service->callStack()) result.frames.push_back({callKindName(frame.kind), frame.callAddress, frame.targetAddress, frame.returnAddress, frame.stackPointer});
    return nlohmann::json(result);
  });
  router.add("inspector", contract::INSPECTOR_PROFILER_START_REQUEST, [service](const nlohmann::json& params, const platform::CancellationToken&) {
    static_cast<void>(contract::parseInspectorProfilerStartParams(params, "params"));
    service->startProfiler();
    return nlohmann::json(contract::InspectorProfilerStartResult{});
  });
  router.add("inspector", contract::INSPECTOR_PROFILER_STOP_REQUEST, [service](const nlohmann::json& params, const platform::CancellationToken&) {
    static_cast<void>(contract::parseInspectorProfilerStopParams(params, "params"));
    const auto report = service->stopProfiler();
    return nlohmann::json(contract::InspectorProfilerStopResult{report.active, static_cast<std::int64_t>(report.totalCycles), profileEntriesToWire(report)});
  });
  router.add("inspector", contract::INSPECTOR_PROFILER_GET_REQUEST, [service](const nlohmann::json& params, const platform::CancellationToken&) {
    static_cast<void>(contract::parseInspectorProfilerGetParams(params, "params"));
    const auto report = service->profile();
    return nlohmann::json(contract::InspectorProfilerGetResult{report.active, static_cast<std::int64_t>(report.totalCycles), profileEntriesToWire(report)});
  });
  router.add("inspector", contract::INSPECTOR_EVENTS_START_REQUEST, [service](const nlohmann::json& params, const platform::CancellationToken&) {
    const auto parsed = contract::parseInspectorEventsStartParams(params, "params");
    service->startEventLog(static_cast<std::uint32_t>(parsed.maxEvents.value_or(DEFAULT_EVENT_CAPACITY)));
    return nlohmann::json(contract::InspectorEventsStartResult{});
  });
  router.add("inspector", contract::INSPECTOR_EVENTS_STOP_REQUEST, [service](const nlohmann::json& params, const platform::CancellationToken&) {
    static_cast<void>(contract::parseInspectorEventsStopParams(params, "params"));
    service->stopEventLog();
    return nlohmann::json(contract::InspectorEventsStopResult{});
  });
  router.add("inspector", contract::INSPECTOR_EVENTS_GET_REQUEST, [service](const nlohmann::json& params, const platform::CancellationToken&) {
    const auto parsed = contract::parseInspectorEventsGetParams(params, "params");
    std::optional<std::uint64_t> fromFrame;
    if (parsed.fromFrame.has_value()) fromFrame = static_cast<std::uint64_t>(*parsed.fromFrame);
    const auto output = service->eventLog(fromFrame, static_cast<std::uint32_t>(parsed.maxEvents.value_or(DEFAULT_EVENT_READ)));
    contract::InspectorEventsGetResult result;
    result.active = output.active;
    result.frame = static_cast<std::int64_t>(output.frame);
    result.dropped = static_cast<std::int64_t>(output.dropped);
    result.events.reserve(output.events.size());
    for (const auto& event : output.events) {
      result.events.push_back({static_cast<std::int64_t>(event.frame), event.scanline, event.dot, eventKindName(event.kind), event.address, event.value, event.pc, static_cast<std::int64_t>(event.cycles)});
    }
    return nlohmann::json(result);
  });
  router.add("inspector", contract::INSPECTOR_GET_PALETTE_REQUEST, [service](const nlohmann::json& params, const platform::CancellationToken&) {
    static_cast<void>(contract::parseInspectorGetPaletteParams(params, "params"));
    auto bytes = service->palette();
    contract::InspectorGetPaletteResult result;
    result.count = static_cast<std::int64_t>(bytes.size() / 4);
    nlohmann::json json(result);
    json["data"] = nlohmann::json::binary(std::move(bytes));
    return json;
  });
  router.add("inspector", contract::INSPECTOR_TRACE_READ_REQUEST, [service](const nlohmann::json& params, const platform::CancellationToken&) {
    const auto parsed = contract::parseInspectorTraceReadParams(params, "params");
    const auto slice = service->readRecording(static_cast<std::uint32_t>(parsed.id), static_cast<std::uint64_t>(parsed.from.value_or(0)), static_cast<std::uint32_t>(parsed.maxLines.value_or(DEFAULT_TRACE_READ_LINES)));
    contract::InspectorTraceReadResult result;
    result.first = static_cast<std::int64_t>(slice.first);
    result.total = static_cast<std::int64_t>(slice.total);
    result.oldest = static_cast<std::int64_t>(slice.oldest);
    result.truncated = slice.truncated;
    result.lines = slice.lines;
    return nlohmann::json(result);
  });
  router.add("inspector", contract::INSPECTOR_LIST_BREAKPOINTS_REQUEST, [service](const nlohmann::json& params, const platform::CancellationToken&) {
    static_cast<void>(contract::parseInspectorListBreakpointsParams(params, "params"));
    contract::InspectorListBreakpointsResult result;
    for (const auto& breakpoint : service->listBreakpoints()) result.breakpoints.push_back(breakpointToWire(breakpoint));
    return nlohmann::json(result);
  });
  router.add("inspector", contract::INSPECTOR_REMOVE_BREAKPOINT_REQUEST, [service](const nlohmann::json& params, const platform::CancellationToken&) {
    const auto parsed = contract::parseInspectorRemoveBreakpointParams(params, "params");
    service->removeBreakpoint(static_cast<std::uint32_t>(parsed.id));
    return nlohmann::json(contract::InspectorRemoveBreakpointResult{});
  });
  router.add("inspector", contract::INSPECTOR_CLEAR_BREAKPOINTS_REQUEST, [service](const nlohmann::json& params, const platform::CancellationToken&) {
    static_cast<void>(contract::parseInspectorClearBreakpointsParams(params, "params"));
    return nlohmann::json(contract::InspectorClearBreakpointsResult{static_cast<std::int64_t>(service->clearBreakpoints())});
  });
  router.add("inspector", contract::INSPECTOR_SET_STATE_REQUEST, [service](const nlohmann::json& params, const platform::CancellationToken&) {
    const auto parsed = contract::parseInspectorSetStateParams(params, "params");
    std::vector<contracts::hardware::StateField> fields;
    for (const auto& field : parsed.fields) fields.push_back({field.name, field.value, field.description.value_or("")});
    return nlohmann::json(contract::InspectorSetStateResult{toWire(service->setState(parsed.module, fields))});
  });
}

}  // namespace ember::requests::inspector
