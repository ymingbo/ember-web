/*
 * ------------------------------------------------------------------------
 * 名称：plugin-info.hpp
 * 说明：把已加载的插件转成 mappers 请求返回的摘要与详情。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * ------------------------------------------------------------------------
 */

#pragma once

#include "contracts/mappers/generated/mappers.hpp"
#include "infra/mappers/loaded-plugin.hpp"

namespace ember::infra::mappers {

/** 插件的来源。 */
enum class PluginSource { Bundled, User, Development };

/** 插件摘要。 */
[[nodiscard]] contracts::mappers::MapperSummary makeSummary(const LoadedPlugin& plugin, PluginSource source, bool enabled);

/** 插件详情：摘要加清单的各部分。 */
[[nodiscard]] contracts::mappers::MapperDetail makeDetail(const LoadedPlugin& plugin, PluginSource source, bool enabled);

}  // namespace ember::infra::mappers
