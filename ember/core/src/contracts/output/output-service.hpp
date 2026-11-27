/*
 * ------------------------------------------------------------------------
 * 名称：output-service.hpp
 * 说明：output 契约：声音输出的设置服务（音量与静音）。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * 备注：设置保存在核心内存里，首选项的持久化以后再做；数据类型由 schema/output.schema.json 生成到 generated/。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <cstdint>
#include <optional>

#include "contracts/output/generated/output.hpp"

namespace ember::contracts::output {

/** 输出设置服务。 */
class OutputService {
 public:
  virtual ~OutputService() = default;

  /**
   * 设置音量与静音，省略的项保持不变；可在任意线程调用。
   * @param volume 音量百分比，0 到 100。
   * @param muted 是否静音。
   * @return 设置之后的音量与静音。
   */
  virtual OutputSetVolumeResult setVolume(std::optional<std::int64_t> volume, std::optional<bool> muted) = 0;

  /** 当前的音量与静音。 */
  [[nodiscard]] virtual OutputSetVolumeResult volume() const = 0;

  /**
   * 设置播放方式："real" 按真实时间播放，"fast" 采样直接丢弃（不等声卡）；可在任意线程调用，不保存。
   * @param mode "real" 或 "fast"，由 Schema 保证。
   * @return 设置之后的方式。
   */
  virtual std::string setAudioMode(const std::string& mode) = 0;
};

}  // namespace ember::contracts::output
