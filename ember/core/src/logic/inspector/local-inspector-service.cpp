/*
 * ------------------------------------------------------------------------
 * 名称：local-inspector-service.cpp
 * 说明：检查器服务的实现，见 local-inspector-service.hpp。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * ------------------------------------------------------------------------
 */

#include "logic/inspector/local-inspector-service.hpp"

#include <algorithm>
#include <string>
#include <utility>

#include "contracts/hardware/hardware-error.hpp"
#include "contracts/inspector/generated/inspector.hpp"
#include "contracts/inspector/inspector-error.hpp"
#include "contracts/output/screenshot-service.hpp"
#include "contracts/runtime/runtime-error.hpp"
#include "shared/errors/core-error.hpp"

namespace ember::logic::inspector {

namespace api = contracts::inspector;
namespace hw = contracts::hardware;
namespace rt = contracts::runtime;

namespace {

/** 卡带存储区空间的前缀。 */
constexpr std::string_view CART_PREFIX = "cart.";

/** 运行与硬件的错误码转成 inspector 的；其余原样返回。 */
std::string translate(const std::string& code) {
  if (code == rt::ERROR_NO_CARTRIDGE) return std::string(api::ERROR_NO_CARTRIDGE);
  if (code == rt::ERROR_NOT_POWERED) return std::string(api::ERROR_NOT_POWERED);
  if (code == rt::ERROR_DEBUG_UNSUPPORTED) return std::string(api::ERROR_UNSUPPORTED);
  if (code == hw::ERROR_UNKNOWN_SPACE) return std::string(api::ERROR_UNKNOWN_SPACE);
  if (code == hw::ERROR_SPACE_OUT_OF_RANGE || code == hw::ERROR_STORAGE_OUT_OF_RANGE) return std::string(api::ERROR_OUT_OF_RANGE);
  if (code == hw::ERROR_SPACE_READ_ONLY) return std::string(api::ERROR_READ_ONLY);
  if (code == hw::ERROR_INVALID_STATE) return std::string(api::ERROR_INVALID_STATE);
  if (code == hw::ERROR_INVALID_BREAKPOINT) return std::string(api::ERROR_INVALID_BREAKPOINT);
  if (code == hw::ERROR_BREAKPOINT_LIMIT) return std::string(api::ERROR_BREAKPOINT_LIMIT);
  if (code == hw::ERROR_INVALID_CONDITION) return std::string(api::ERROR_INVALID_CONDITION);
  if (code == hw::ERROR_RECORDER_LIMIT) return std::string(api::ERROR_RECORDING_LIMIT);
  if (code == hw::ERROR_NO_RECORDER) return std::string(api::ERROR_NO_RECORDING);
  if (code == hw::ERROR_CDL_ACTIVE) return std::string(api::ERROR_CDL_ACTIVE);
  if (code == contracts::output::ERROR_INVALID_FILE_NAME) return std::string(api::ERROR_INVALID_FILE_NAME);
  if (code == contracts::output::ERROR_FILE_FAILED) return std::string(api::ERROR_FILE_FAILED);
  return code;
}

/** 卡带存储区空间的存储区标识；不是 cart. 开头时返回空。 */
std::optional<std::string> cartStorageId(const std::string& space) {
  if (!space.starts_with(CART_PREFIX)) return std::nullopt;
  return space.substr(CART_PREFIX.size());
}

/** 按空间标识取卡带的存储区；没有时抛出 inspector.unknownSpace。 */
const hw::StorageRegion& cartStorage(rt::DebugContext& context, const std::string& id, const std::string& space) {
  const hw::StorageRegion* region = context.cartridge == nullptr ? nullptr : context.cartridge->storage(id);
  if (region == nullptr) throw shared::CoreError(hw::ERROR_UNKNOWN_SPACE, "没有这个内存空间：" + space);
  return *region;
}

void checkRange(const std::string& space, std::size_t size, std::uint32_t address, std::size_t length) {
  if (address > size || length > size - address) {
    throw shared::CoreError(hw::ERROR_SPACE_OUT_OF_RANGE, "空间 " + space + " 的大小是 " + std::to_string(size) + " 字节，访问 " + std::to_string(address) + " 起的 " + std::to_string(length) + " 字节超出范围");
  }
}

/** 卡带脚本状态。 */
api::StateReport mapperState(const contracts::mappers::MapperCartridge& cartridge) {
  api::StateReport state{"mapper", false, {}};
  for (const std::string& name : cartridge.stateNames()) state.fields.push_back({name, static_cast<std::int64_t>(cartridge.state(name).value_or(0)), "卡带脚本的内部状态（只读）"});
  return state;
}

/** 各模块的状态；module 不为空时只取这一个，没有这个模块时抛出 hardware.invalidState。 */
std::vector<api::StateReport> collectStates(rt::DebugContext& context, const std::optional<std::string>& module) {
  std::vector<api::StateReport> states;
  for (const std::string& name : context.debugger.stateModules()) {
    if (module.has_value() && *module != name) continue;
    states.push_back({name, name == "cpu", context.debugger.readState(name)});
  }
  if (context.cartridge != nullptr && (!module.has_value() || *module == "mapper")) states.push_back(mapperState(*context.cartridge));
  if (module.has_value() && states.empty()) throw shared::CoreError(hw::ERROR_INVALID_STATE, "没有这个状态模块：" + *module);
  return states;
}

/** 标记的统计。 */
api::CdlReport cdlStatistics(const std::vector<std::uint8_t>& flags) {
  api::CdlReport report;
  report.size = flags.size();
  for (const std::uint8_t flag : flags) {
    if ((flag & hw::CDL_CODE) != 0) ++report.codeBytes;
    if ((flag & hw::CDL_DATA) != 0) ++report.dataBytes;
  }
  return report;
}

}  // namespace

LocalInspectorService::LocalInspectorService(std::shared_ptr<rt::RuntimeService> runtime, std::shared_ptr<platform::EventSink> events, std::shared_ptr<contracts::output::DebugFileWriter> files)
    : runtime_(std::move(runtime)), events_(std::move(events)), files_(std::move(files)) {
  runtime_->setObserver([this](rt::DebugContext& context) {
    if (watcher_.list().empty()) return;
    const auto cycles = static_cast<std::int64_t>(context.machine.cpuRegisters().cycles);
    for (const auto& change : watcher_.observe(context)) {
      events_->publish(api::INSPECTOR_STATE_CHANGED_EVENT, nlohmann::json(api::InspectorStateChangedData{change.path, change.oldValue, change.newValue, cycles}));
    }
  });
}

LocalInspectorService::~LocalInspectorService() {
  try {
    runtime_->setObserver({});
  } catch (const shared::CoreError&) {
    // 运行服务已停止，观察者随模拟线程一起结束，没有要取消的。
  }
}

void LocalInspectorService::translateErrors(const std::function<void()>& action) {
  try {
    action();
  } catch (const shared::CoreError& error) {
    const std::string code(error.code());
    const std::string translated = translate(code);
    if (translated == code) throw;
    throw shared::CoreError(translated, error.what());
  }
}

void LocalInspectorService::inspect(const std::function<void(rt::DebugContext&)>& action) {
  translateErrors([&] { runtime_->inspect(action); });
}

std::vector<hw::MemorySpace> LocalInspectorService::listSpaces() {
  std::vector<hw::MemorySpace> spaces;
  inspect([&](rt::DebugContext& context) {
    spaces = context.debugger.memorySpaces();
    if (context.cartridge == nullptr) return;
    for (const auto& item : context.cartridge->storageList()) {
      const bool ram = item.kind == hw::StorageKind::Ram;
      spaces.push_back({std::string(CART_PREFIX) + item.id, std::string("卡带的存储区 ") + item.id + (ram ? "（RAM）" : "（ROM，不能写）"), item.size, ram});
    }
  });
  return spaces;
}

std::vector<std::uint8_t> LocalInspectorService::readMemory(const std::string& space, std::uint32_t address, std::uint32_t length) {
  std::vector<std::uint8_t> bytes(length);
  inspect([&](rt::DebugContext& context) {
    if (const auto id = cartStorageId(space)) {
      const hw::StorageRegion& region = cartStorage(context, *id, space);
      checkRange(space, region.size(), address, length);
      region.readBlock(address, bytes);
    } else {
      context.debugger.readMemory(space, address, bytes);
    }
  });
  return bytes;
}

std::size_t LocalInspectorService::writeMemory(const std::string& space, std::uint32_t address, std::span<const std::uint8_t> bytes) {
  inspect([&](rt::DebugContext& context) {
    if (const auto id = cartStorageId(space)) {
      const hw::StorageRegion& region = cartStorage(context, *id, space);
      checkRange(space, region.size(), address, bytes.size());
      if (region.kind() == hw::StorageKind::Rom) throw shared::CoreError(hw::ERROR_SPACE_READ_ONLY, "空间 " + space + " 是 ROM，不能写入");
      for (std::size_t index = 0; index < bytes.size(); ++index) context.cartridge->pokeStorage(*id, address + index, bytes[index]);
    } else {
      context.debugger.writeMemory(space, address, bytes);
    }
  });
  return bytes.size();
}

hw::AddressTarget LocalInspectorService::resolve(const std::string& space, std::uint32_t address) {
  hw::AddressTarget info;
  inspect([&](rt::DebugContext& context) {
    info = context.debugger.resolve(space, address);
    if (info.kind != "cartridge") return;
    if (context.cartridge == nullptr) {
      info = {"none", "", 0, "没有插卡带，没有设备响应"};
      return;
    }
    using Kind = contracts::mappers::AddressInfo::Kind;
    const auto described = context.cartridge->describeAddress(space == "ppu", static_cast<std::uint16_t>(address));
    switch (described.kind) {
      case Kind::Storage: {
        if (!described.mapped) {
          info = {"none", "", 0, "卡带的窗口当前没有映射，没有设备响应"};
          return;
        }
        const hw::StorageRegion* region = context.cartridge->storage(described.storage);
        const bool ram = region != nullptr && region->kind() == hw::StorageKind::Ram;
        info = {ram ? "ram" : "rom", std::string(CART_PREFIX) + described.storage, described.offset, std::string("卡带的存储区 ") + described.storage + "，经窗口映射"};
        break;
      }
      case Kind::Register:
        info = {"register", "cart.mapper", 0, "mapper 脚本的寄存器，读写都会回调脚本"};
        break;
      case Kind::Nametable:
        info = {"ram", "cart.nametable", described.offset, "卡带自带的名称表显存（四屏）"};
        break;
      case Kind::None:
        info = {"none", "", 0, "卡带不响应这个地址，读到总线上最近的值"};
        break;
    }
  });
  return info;
}

std::vector<api::StateReport> LocalInspectorService::getState(const std::optional<std::string>& module) {
  std::vector<api::StateReport> states;
  inspect([&](rt::DebugContext& context) { states = collectStates(context, module); });
  return states;
}

api::SnapshotReport LocalInspectorService::snapshot(const std::optional<std::string>& module) {
  api::SnapshotReport report;
  inspect([&](rt::DebugContext& context) {
    report.cycles = context.machine.cpuRegisters().cycles;
    report.states = collectStates(context, module);
    report.breakpoints = context.debugger.breakpoints();
    report.watches = watcher_.list();
  });
  return report;
}

api::WatchEntry LocalInspectorService::watch(const std::string& path) {
  api::WatchEntry entry;
  inspect([&](rt::DebugContext& context) { entry = watcher_.add(context, path); });
  return entry;
}

void LocalInspectorService::unwatch(const std::string& path) {
  inspect([&](rt::DebugContext&) { watcher_.remove(path); });
}

api::StateReport LocalInspectorService::setState(const std::string& module, std::span<const hw::StateField> fields) {
  api::StateReport state;
  inspect([&](rt::DebugContext& context) {
    if (module == "mapper") throw shared::CoreError(hw::ERROR_SPACE_READ_ONLY, "mapper 的状态只读");
    context.debugger.writeState(module, fields);
    state = {module, module == "cpu", context.debugger.readState(module)};
  });
  return state;
}

std::vector<hw::DisassembledInstruction> LocalInspectorService::disassemble(std::uint16_t address, std::uint32_t count) {
  std::vector<hw::DisassembledInstruction> list;
  inspect([&](rt::DebugContext& context) { list = context.debugger.disassemble(address, count); });
  return list;
}

api::CdlReport LocalInspectorService::startCdl() {
  api::CdlReport report;
  inspect([&](rt::DebugContext& context) {
    contracts::mappers::MapperCartridge* cartridge = context.cartridge;
    const hw::StorageRegion* prg = cartridge == nullptr ? nullptr : cartridge->storage("prg");
    if (prg == nullptr) throw shared::CoreError(api::ERROR_UNSUPPORTED, "卡带没有 PRG-ROM 存储区，无法做代码与数据标记");
    // 换算按调用时的分页，标记的是物理位置；只标 PRG-ROM 里的字节。
    context.debugger.startCdl(prg->size(), [cartridge](std::uint16_t address) -> std::optional<std::size_t> {
      const auto described = cartridge->describeAddress(false, address);
      if (described.kind != contracts::mappers::AddressInfo::Kind::Storage || !described.mapped || described.storage != "prg") return std::nullopt;
      return described.offset;
    });
    report.size = prg->size();
  });
  return report;
}

api::CdlReport LocalInspectorService::stopCdl() {
  api::CdlReport report;
  inspect([&](rt::DebugContext& context) {
    const auto flags = context.debugger.cdlFlags();
    if (!flags.has_value()) throw shared::CoreError(api::ERROR_NO_CDL, "还没有记录过代码与数据标记");
    context.debugger.stopCdl();
    report = cdlStatistics(*flags);
  });
  return report;
}

api::CdlReport LocalInspectorService::saveCdl(const std::string& fileName) {
  const std::string name = fileName.empty() ? "prg.cdl" : fileName;
  translateErrors([&] { files_->validateName(name); });
  std::vector<std::uint8_t> flags;
  inspect([&](rt::DebugContext& context) {
    auto current = context.debugger.cdlFlags();
    if (!current.has_value()) throw shared::CoreError(api::ERROR_NO_CDL, "还没有记录过代码与数据标记");
    flags = std::move(*current);
  });
  api::CdlReport report = cdlStatistics(flags);
  translateErrors([&] { report.file = files_->writeBytes(name, flags); });
  return report;
}

std::uint32_t LocalInspectorService::startRecording(const hw::RecorderSpec& spec, const std::string& fileName) {
  if (!fileName.empty()) translateErrors([&] { files_->validateName(fileName); });
  std::uint32_t id = 0;
  inspect([&](rt::DebugContext& context) { id = context.debugger.startRecorder(spec); });
  const bool callLog = spec.kind == hw::RecorderKind::Call;
  Recording recording{fileName.empty() ? std::string(callLog ? "calllog-" : "trace-") + std::to_string(id) + ".txt" : fileName, callLog};
  std::lock_guard lock(recordingsMutex_);
  recordings_[id] = std::move(recording);
  return id;
}

api::RecordingReport LocalInspectorService::stopRecording(std::uint32_t id, bool callLog) {
  Recording recording;
  {
    std::lock_guard lock(recordingsMutex_);
    const auto found = recordings_.find(id);
    if (found == recordings_.end() || found->second.callLog != callLog) throw shared::CoreError(api::ERROR_NO_RECORDING, "没有编号为 " + std::to_string(id) + " 的" + (callLog ? "调用记录" : "跟踪"));
    recording = found->second;
  }
  hw::RecorderOutput output;
  try {
    inspect([&](rt::DebugContext& context) { output = context.debugger.stopRecorder(id); });
  } catch (const shared::CoreError& error) {
    // 换了卡带，主机上已经没有这项记录。
    if (error.code() == api::ERROR_NO_RECORDING) {
      std::lock_guard lock(recordingsMutex_);
      recordings_.erase(id);
    }
    throw;
  }
  {
    std::lock_guard lock(recordingsMutex_);
    recordings_.erase(id);
  }
  api::RecordingReport report;
  translateErrors([&] { report.file = files_->writeLines(recording.fileName, output.lines); });
  report.lines = output.lines.size();
  report.truncated = output.truncated;
  return report;
}

hw::Breakpoint LocalInspectorService::addBreakpoint(hw::BreakpointKind kind, std::uint16_t first, std::uint16_t last, const std::string& condition) {
  hw::Breakpoint added;
  inspect([&](rt::DebugContext& context) { added = context.debugger.addBreakpoint(kind, first, last, condition); });
  return added;
}

hw::Breakpoint LocalInspectorService::updateBreakpoint(std::uint32_t id, std::optional<bool> enabled, const std::optional<std::string>& condition) {
  hw::Breakpoint updated;
  inspect([&](rt::DebugContext& context) {
    const auto result = context.debugger.updateBreakpoint(id, enabled, condition);
    if (!result.has_value()) throw shared::CoreError(api::ERROR_NO_BREAKPOINT, "没有编号为 " + std::to_string(id) + " 的断点");
    updated = *result;
  });
  return updated;
}

std::vector<hw::Breakpoint> LocalInspectorService::listBreakpoints() {
  std::vector<hw::Breakpoint> list;
  inspect([&](rt::DebugContext& context) { list = context.debugger.breakpoints(); });
  return list;
}

void LocalInspectorService::removeBreakpoint(std::uint32_t id) {
  inspect([&](rt::DebugContext& context) {
    if (!context.debugger.removeBreakpoint(id)) throw shared::CoreError(api::ERROR_NO_BREAKPOINT, "没有编号为 " + std::to_string(id) + " 的断点");
  });
}

std::size_t LocalInspectorService::clearBreakpoints() {
  std::size_t removed = 0;
  inspect([&](rt::DebugContext& context) { removed = context.debugger.clearBreakpoints(); });
  return removed;
}

hw::RecorderSlice LocalInspectorService::readRecording(std::uint32_t id, std::uint64_t from, std::uint32_t maxLines) {
  hw::RecorderSlice slice;
  inspect([&](rt::DebugContext& context) { slice = context.debugger.readRecorder(id, from, maxLines); });
  return slice;
}

std::vector<hw::CallFrame> LocalInspectorService::callStack() {
  std::vector<hw::CallFrame> frames;
  inspect([&](rt::DebugContext& context) { frames = context.debugger.callStack(); });
  return frames;
}

void LocalInspectorService::startProfiler() {
  inspect([&](rt::DebugContext& context) { context.debugger.startProfiler(); });
}

hw::ProfileReport LocalInspectorService::stopProfiler() {
  hw::ProfileReport report;
  inspect([&](rt::DebugContext& context) {
    if (!context.debugger.profile().has_value()) throw shared::CoreError(api::ERROR_NO_PROFILE, "还没有开始过子程序统计");
    context.debugger.stopProfiler();
    report = *context.debugger.profile();
  });
  return report;
}

hw::ProfileReport LocalInspectorService::profile() {
  hw::ProfileReport report;
  inspect([&](rt::DebugContext& context) {
    auto current = context.debugger.profile();
    if (!current.has_value()) throw shared::CoreError(api::ERROR_NO_PROFILE, "还没有开始过子程序统计");
    report = std::move(*current);
  });
  return report;
}

void LocalInspectorService::startEventLog(std::uint32_t maxEvents) {
  inspect([&](rt::DebugContext& context) { context.debugger.startEventLog(maxEvents); });
}

void LocalInspectorService::stopEventLog() {
  inspect([&](rt::DebugContext& context) {
    if (!context.debugger.eventLog(std::nullopt, 1).has_value()) throw shared::CoreError(api::ERROR_NO_EVENT_LOG, "还没有开始过事件记录");
    context.debugger.stopEventLog();
  });
}

hw::EventLogOutput LocalInspectorService::eventLog(std::optional<std::uint64_t> fromFrame, std::uint32_t maxEvents) {
  hw::EventLogOutput output;
  inspect([&](rt::DebugContext& context) {
    auto current = context.debugger.eventLog(fromFrame, maxEvents);
    if (!current.has_value()) throw shared::CoreError(api::ERROR_NO_EVENT_LOG, "还没有开始过事件记录");
    output = std::move(*current);
  });
  return output;
}

std::vector<std::uint8_t> LocalInspectorService::palette() {
  std::vector<std::uint8_t> bytes;
  inspect([&](rt::DebugContext& context) {
    for (const std::uint32_t color : context.debugger.colorTable()) {
      bytes.push_back(static_cast<std::uint8_t>(color & 0xFFU));
      bytes.push_back(static_cast<std::uint8_t>((color >> 8) & 0xFFU));
      bytes.push_back(static_cast<std::uint8_t>((color >> 16) & 0xFFU));
      bytes.push_back(static_cast<std::uint8_t>((color >> 24) & 0xFFU));
    }
  });
  return bytes;
}

}  // namespace ember::logic::inspector
