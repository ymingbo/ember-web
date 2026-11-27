/*
 * ------------------------------------------------------------------------
 * 名称：local-inspector-service.hpp
 * 说明：检查器服务的实现：经运行服务的调试访问在模拟线程读写主机与卡带，把卡带上的存储区合成 cart.<标识> 空间，把运行与硬件的错误码转成 inspector 的错误码。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * ------------------------------------------------------------------------
 */

#pragma once

#include <map>
#include <memory>
#include <mutex>

#include "logic/inspector/state-watcher.hpp"
#include "platform/events/event-sink.hpp"

#include "contracts/inspector/inspector-service.hpp"
#include "contracts/output/debug-file-writer.hpp"
#include "contracts/runtime/runtime-service.hpp"

namespace ember::logic::inspector {

/** 检查器服务。 */
class LocalInspectorService final : public contracts::inspector::InspectorService {
 public:
  /** 向运行服务登记观察者，用来检查监视项并发 inspector.stateChanged。 */
  LocalInspectorService(std::shared_ptr<contracts::runtime::RuntimeService> runtime, std::shared_ptr<platform::EventSink> events, std::shared_ptr<contracts::output::DebugFileWriter> files);
  ~LocalInspectorService() override;

  LocalInspectorService(const LocalInspectorService&) = delete;
  LocalInspectorService& operator=(const LocalInspectorService&) = delete;

  std::vector<contracts::hardware::MemorySpace> listSpaces() override;
  std::vector<std::uint8_t> readMemory(const std::string& space, std::uint32_t address, std::uint32_t length) override;
  std::size_t writeMemory(const std::string& space, std::uint32_t address, std::span<const std::uint8_t> bytes) override;
  contracts::hardware::AddressTarget resolve(const std::string& space, std::uint32_t address) override;
  std::vector<contracts::inspector::StateReport> getState(const std::optional<std::string>& module) override;
  contracts::inspector::StateReport setState(const std::string& module, std::span<const contracts::hardware::StateField> fields) override;
  contracts::inspector::SnapshotReport snapshot(const std::optional<std::string>& module) override;
  contracts::inspector::WatchEntry watch(const std::string& path) override;
  void unwatch(const std::string& path) override;
  std::vector<contracts::hardware::DisassembledInstruction> disassemble(std::uint16_t address, std::uint32_t count) override;
  contracts::inspector::CdlReport startCdl() override;
  contracts::inspector::CdlReport stopCdl() override;
  contracts::inspector::CdlReport saveCdl(const std::string& fileName) override;
  std::uint32_t startRecording(const contracts::hardware::RecorderSpec& spec, const std::string& fileName) override;
  contracts::inspector::RecordingReport stopRecording(std::uint32_t id, bool callLog) override;
  contracts::hardware::RecorderSlice readRecording(std::uint32_t id, std::uint64_t from, std::uint32_t maxLines) override;
  contracts::hardware::Breakpoint addBreakpoint(contracts::hardware::BreakpointKind kind, std::uint16_t first, std::uint16_t last, const std::string& condition) override;
  contracts::hardware::Breakpoint updateBreakpoint(std::uint32_t id, std::optional<bool> enabled, const std::optional<std::string>& condition) override;
  std::vector<contracts::hardware::Breakpoint> listBreakpoints() override;
  void removeBreakpoint(std::uint32_t id) override;
  std::size_t clearBreakpoints() override;
  std::vector<contracts::hardware::CallFrame> callStack() override;
  void startProfiler() override;
  contracts::hardware::ProfileReport stopProfiler() override;
  contracts::hardware::ProfileReport profile() override;
  void startEventLog(std::uint32_t maxEvents) override;
  void stopEventLog() override;
  contracts::hardware::EventLogOutput eventLog(std::optional<std::uint64_t> fromFrame, std::uint32_t maxEvents) override;
  std::vector<std::uint8_t> palette() override;

 private:
  /** 在模拟线程执行回调，并把错误码转成 inspector 的。 */
  void inspect(const std::function<void(contracts::runtime::DebugContext&)>& action);
  /** 执行动作，把运行、硬件与 output 的错误码转成 inspector 的。 */
  static void translateErrors(const std::function<void()>& action);

  /** 进行中的记录：写文件用的名字与种类。 */
  struct Recording {
    std::string fileName;
    bool callLog{false};
  };

  std::shared_ptr<contracts::runtime::RuntimeService> runtime_;
  std::shared_ptr<platform::EventSink> events_;
  std::shared_ptr<contracts::output::DebugFileWriter> files_;
  std::mutex recordingsMutex_;
  std::map<std::uint32_t, Recording> recordings_;
  /** 只在模拟线程访问。 */
  StateWatcher watcher_;
};

}  // namespace ember::logic::inspector
