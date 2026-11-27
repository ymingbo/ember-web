/*
 * ------------------------------------------------------------------------
 * 名称：loaded-plugin.hpp
 * 说明：已通过检查的插件：清单、脚本与图标标志。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * ------------------------------------------------------------------------
 */

#pragma once

#include <string>

#include "infra/mappers/manifest.hpp"

namespace ember::infra::mappers {

/** 已通过检查的插件。 */
struct LoadedPlugin {
  Manifest manifest;
  /** main.js 的内容；没有脚本时为空。 */
  std::string script;
  /** 包里是否有 icon.png。 */
  bool hasIcon{false};
};

}  // namespace ember::infra::mappers
