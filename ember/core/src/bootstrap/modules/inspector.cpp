/*
 * ------------------------------------------------------------------------
 * 名称：inspector.cpp
 * 说明：检查器模块的登记：创建检查器服务并登记 inspector.* 请求。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * ------------------------------------------------------------------------
 */

#include "bootstrap/modules/inspector.hpp"

#include <memory>

#include "contracts/inspector/inspector-service.hpp"
#include "contracts/output/debug-file-writer.hpp"
#include "contracts/runtime/runtime-service.hpp"
#include "logic/inspector/local-inspector-service.hpp"
#include "requests/inspector/inspector-requests.hpp"

namespace ember::bootstrap {

void registerInspector(ModuleRegistry& registry) {
  auto runtime = registry.services.require<contracts::runtime::RuntimeService>();
  auto files = registry.services.require<contracts::output::DebugFileWriter>();
  auto service = std::make_shared<logic::inspector::LocalInspectorService>(std::move(runtime), registry.events, std::move(files));
  registry.services.provide<contracts::inspector::InspectorService>(service);
  requests::inspector::addInspectorRequests(registry.requests, service);
}

}  // namespace ember::bootstrap
