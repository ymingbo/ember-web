/*
 * ------------------------------------------------------------------------
 * 名称：output.hpp
 * 说明：输出模块的登记函数声明，以及声音输出的打开与降级函数。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * ------------------------------------------------------------------------
 */

#pragma once

#include <functional>
#include <memory>
#include <string>

#include "bootstrap/module-registry.hpp"
#include "contracts/preferences/preference-service.hpp"
#include "platform/audio/audio-output.hpp"
#include "platform/logging/error-log.hpp"

namespace ember::bootstrap {

/** 声音输出的打开函数：成功返回输出对象，失败返回空指针并在 failure 里写原因；可以抛出异常。 */
using AudioOpener = std::function<std::shared_ptr<platform::AudioOutput>(unsigned sampleRate, std::string& failure)>;

/**
 * 打开声音输出；打不开（没有声卡、设备被独占，或打开函数抛出异常）时向错误日志写一条 `output.deviceUnavailable`
 * （上下文里有原因），降级为空输出，不抛出异常。
 */
std::shared_ptr<platform::AudioOutput> openAudioOutputOrFallback(const AudioOpener& opener, unsigned sampleRate, platform::ErrorLog& errors);

/** 登记声音分组与设置项：缓冲大小、启动时静音、音量（隐藏，只保存）。 */
void registerSoundSettings(contracts::preferences::PreferenceService& service);

/** 登记输出模块。 */
void registerOutput(ModuleRegistry& registry);

}  // namespace ember::bootstrap
