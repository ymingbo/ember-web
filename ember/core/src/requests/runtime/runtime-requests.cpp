/*
 * ------------------------------------------------------------------------
 * 名称：runtime-requests.cpp
 * 说明：runtime 模块的请求登记实现：校验参数，调用运行服务，返回结果。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * ------------------------------------------------------------------------
 */

#include "requests/runtime/runtime-requests.hpp"

#include <chrono>
#include <future>

#include "shared/errors/core-error.hpp"

namespace ember::requests::runtime {

namespace contract = ::ember::contracts::runtime;

void addRuntimeRequests(platform::RequestRouter& router, std::shared_ptr<contract::RuntimeService> service) {
  router.add("runtime", contract::RUNTIME_STATUS_REQUEST, [service](const nlohmann::json& params, const platform::CancellationToken&) {
    static_cast<void>(contract::parseRuntimeStatusParams(params, "params"));
    return nlohmann::json(service->status());
  });
  router.add("runtime", contract::RUNTIME_POWER_ON_REQUEST, [service](const nlohmann::json& params, const platform::CancellationToken&) {
    const auto parsed = contract::parseRuntimePowerOnParams(params, "params");
    return nlohmann::json(contract::RuntimePowerOnResult{service->powerOn(parsed.resume.value_or(false))});
  });
  router.add("runtime", contract::RUNTIME_POWER_OFF_REQUEST, [service](const nlohmann::json& params, const platform::CancellationToken&) {
    static_cast<void>(contract::parseRuntimePowerOffParams(params, "params"));
    return nlohmann::json(contract::RuntimePowerOffResult{service->powerOff()});
  });
  router.add("runtime", contract::RUNTIME_RESET_REQUEST, [service](const nlohmann::json& params, const platform::CancellationToken&) {
    const auto parsed = contract::parseRuntimeResetParams(params, "params");
    const auto kind = parsed.kind == "soft" ? contract::ResetKind::Soft : contract::ResetKind::Hard;
    return nlohmann::json(contract::RuntimeResetResult{service->reset(kind)});
  });
  router.add("runtime", contract::RUNTIME_PAUSE_REQUEST, [service](const nlohmann::json& params, const platform::CancellationToken&) {
    static_cast<void>(contract::parseRuntimePauseParams(params, "params"));
    return nlohmann::json(contract::RuntimePauseResult{service->pause()});
  });
  router.add("runtime", contract::RUNTIME_RESUME_REQUEST, [service](const nlohmann::json& params, const platform::CancellationToken&) {
    static_cast<void>(contract::parseRuntimeResumeParams(params, "params"));
    return nlohmann::json(contract::RuntimeResumeResult{service->resume()});
  });
  router.add("runtime", contract::RUNTIME_RUN_REQUEST, [service](const nlohmann::json& params, const platform::CancellationToken& token) {
    const auto parsed = contract::parseRuntimeRunParams(params, "params");
    contract::RunQuota quota;
    int given = 0;
    if (parsed.frames.has_value()) {
      quota = {contract::RunUnit::Frames, *parsed.frames};
      ++given;
    }
    if (parsed.cycles.has_value()) {
      quota = {contract::RunUnit::Cycles, *parsed.cycles};
      ++given;
    }
    if (parsed.instructions.has_value()) {
      quota = {contract::RunUnit::Instructions, *parsed.instructions};
      ++given;
    }
    if (parsed.scanlines.has_value()) {
      quota = {contract::RunUnit::Scanlines, *parsed.scanlines};
      ++given;
    }
    if (given != 1) throw shared::SchemaError("frames、cycles、instructions、scanlines 必须给且只能给一个");
    auto outcome = service->startRun(quota);
    contract::RuntimeRunResult result;
    if (parsed.wait.value_or(false)) {
      // 等运行结束；请求被取消（连接断开、服务停止）时打断运行，结果给出已经跑的部分。
      while (outcome.wait_for(std::chrono::milliseconds(20)) != std::future_status::ready) {
        if (token.isCancelled()) {
          service->cancelRun();
          break;
        }
      }
      const auto& finished = outcome.get();
      result.frames = finished.frames;
      result.cycles = finished.cycles;
      result.instructions = finished.instructions;
      result.stopped = finished.stopped;
    }
    const auto status = service->status();
    result.state = contract::RuntimeState{status.power, status.mode};
    return nlohmann::json(result);
  });
  router.add("runtime", contract::RUNTIME_STEP_REQUEST, [service](const nlohmann::json& params, const platform::CancellationToken&) {
    const auto parsed = contract::parseRuntimeStepParams(params, "params");
    namespace hw = ::ember::contracts::hardware;
    hw::StepUnit unit = hw::StepUnit::Instruction;
    if (parsed.unit == "cycle") unit = hw::StepUnit::Cycle;
    else if (parsed.unit == "scanline") unit = hw::StepUnit::Scanline;
    else if (parsed.unit == "frame") unit = hw::StepUnit::Frame;
    const auto outcome = service->step(unit, parsed.count.value_or(1));
    contract::RuntimeStepResult result;
    result.state = outcome.state;
    result.instructions = outcome.instructions;
    result.cycles = outcome.cycles;
    result.frames = outcome.frames;
    result.pc = outcome.pc;
    return nlohmann::json(result);
  });
  router.add("runtime", contract::RUNTIME_SET_SPEED_REQUEST, [service](const nlohmann::json& params, const platform::CancellationToken&) {
    const auto parsed = contract::parseRuntimeSetSpeedParams(params, "params");
    const bool max = parsed.max.value_or(false);
    if (max && parsed.percent.has_value()) throw shared::SchemaError("percent 与 max 不能同时给");
    contract::SpeedSetting speed;
    // 不限速时百分比保持原值，取消不限速时按给的百分比，都不给就恢复 100%。
    speed.max = max;
    speed.percent = max ? service->status().speed : parsed.percent.value_or(100);
    const auto applied = service->setSpeed(speed);
    return nlohmann::json(contract::RuntimeSetSpeedResult{applied.percent, applied.max});
  });
  router.add("runtime", contract::RUNTIME_SET_FRAME_EVENTS_REQUEST, [service](const nlohmann::json& params, const platform::CancellationToken&) {
    const auto parsed = contract::parseRuntimeSetFrameEventsParams(params, "params");
    service->setFrameEventInterval(static_cast<std::uint32_t>(parsed.frames));
    return nlohmann::json(contract::RuntimeSetFrameEventsResult{parsed.frames});
  });
}

}  // namespace ember::requests::runtime
