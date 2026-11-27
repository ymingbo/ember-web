/*
 * ------------------------------------------------------------------------
 * 名称：output-requests.cpp
 * 说明：output 模块的请求登记实现：校验参数，调用输出设置服务，返回结果。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * ------------------------------------------------------------------------
 */

#include "requests/output/output-requests.hpp"

namespace ember::requests::output {

namespace contract = ::ember::contracts::output;

void addOutputRequests(platform::RequestRouter& router, std::shared_ptr<contract::OutputService> service, std::shared_ptr<contract::ScreenshotService> screenshots) {
  router.add("output", contract::OUTPUT_SET_VOLUME_REQUEST, [service](const nlohmann::json& params, const platform::CancellationToken&) {
    const auto parsed = contract::parseOutputSetVolumeParams(params, "params");
    return nlohmann::json(service->setVolume(parsed.volume, parsed.muted));
  });
  router.add("output", contract::OUTPUT_SET_AUDIO_MODE_REQUEST, [service](const nlohmann::json& params, const platform::CancellationToken&) {
    const auto parsed = contract::parseOutputSetAudioModeParams(params, "params");
    return nlohmann::json(contract::OutputSetAudioModeResult{service->setAudioMode(parsed.mode)});
  });
  router.add("output", contract::OUTPUT_SCREENSHOT_REQUEST, [screenshots](const nlohmann::json& params, const platform::CancellationToken&) {
    const auto parsed = contract::parseOutputScreenshotParams(params, "params");
    auto shot = screenshots->capture(parsed.format.value_or("png"), parsed.file);
    contract::OutputScreenshotResult result;
    result.format = shot.format;
    result.width = shot.width;
    result.height = shot.height;
    result.byteLength = static_cast<std::int64_t>(shot.byteLength);
    if (!shot.file.empty()) result.file = shot.file;
    nlohmann::json json(result);
    // 没有保存到文件时图像以二进制值放在 data 里，调试服务改发二进制帧。
    if (shot.file.empty()) json["data"] = nlohmann::json::binary(std::move(shot.data));
    return json;
  });
}

}  // namespace ember::requests::output
