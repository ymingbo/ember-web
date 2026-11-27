/*
 * ------------------------------------------------------------------------
 * 名称：runtime.cpp
 * 说明：运行模块的登记：创建模拟线程与命令队列（运行服务）、主机型号目录与独立于主机的磁带机、软驱 A，登记 runtime.* 请求，把运行服务、磁带机与软驱 A 提供给其他模块，核心停止时结束模拟线程。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * 备注：取用 mappers 模块的卡带工厂与 output 模块的声音输出，须排在它们之后登记；卡带模块取用本模块的运行服务，须排在本模块之后。
 *       定时器精度对象与运行服务同生命周期，停止时一起释放。
 * ------------------------------------------------------------------------
 */

#include "bootstrap/modules/runtime.hpp"

#include <memory>

#include "bootstrap/machines/common/floppy-activity-relay.hpp"
#include "bootstrap/machines/common/printer-output-relay.hpp"
#include "bootstrap/machines/machine-catalog.hpp"
#include "contracts/mappers/mapper-factory.hpp"
#include "contracts/hardware/floppy-drive.hpp"
#include "contracts/hardware/tape-deck.hpp"
#include "contracts/runtime/runtime-service.hpp"
#include "logic/runtime/simulation-runtime.hpp"
#include "platform/audio/audio-output.hpp"
#include "platform/time/thread-priority.hpp"
#include "platform/time/timer-resolution.hpp"
#include "requests/runtime/runtime-requests.hpp"

namespace ember::bootstrap {

void registerRuntime(ModuleRegistry& registry) {
  // 整机机型的打印机把页交给转接对象，media 模块（排在本模块之后）再把真正的接收方绑定上去。
  auto printerRelay = std::make_shared<PrinterOutputRelay>();
  registry.services.provide<PrinterOutputRelay>(printerRelay);
  auto floppyRelay = std::make_shared<FloppyActivityRelay>();
  registry.services.provide<FloppyActivityRelay>(floppyRelay);
  // 磁带机是独立于主机的设备：由主机型号目录创建并交给需要的主机，media 模块（排在本模块之后）取用同一台来载入与播放磁带。
  auto catalog = std::make_shared<MachineCatalog>(printerRelay, floppyRelay);
  registry.services.provide<contracts::hardware::TapeControl>(catalog->tapeControl());
  registry.services.provide<contracts::hardware::FloppyDrive>(catalog->floppyDrive());
  logic::runtime::SimulationRuntimeOptions options;
  options.machines = catalog;
  options.mappers = registry.services.require<contracts::mappers::MapperFactory>();
  options.frames = registry.frames;
  options.audio = registry.services.require<platform::AudioOutput>();
  options.events = registry.events;
  options.errors = registry.errors;
  // 帧节拍晚了声音缓冲会被抽空，模拟线程要高于一般线程的优先级。
  options.threadScope = [] { return std::shared_ptr<void>(std::make_shared<platform::ThreadPriorityBoost>()); };
  auto timer = std::make_shared<platform::TimerResolution>();
  auto runtime = std::make_shared<logic::runtime::SimulationRuntime>(std::move(options));
  registry.services.provide<contracts::runtime::RuntimeService>(runtime);
  requests::runtime::addRuntimeRequests(registry.requests, runtime);
  registry.stopHandlers.push_back([runtime, timer] { runtime->stop(); });
}

}  // namespace ember::bootstrap
