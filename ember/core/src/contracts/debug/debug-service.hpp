/*
 * ------------------------------------------------------------------------
 * 名称：debug-service.hpp
 * 说明：debug 契约：调试服务的状态查询。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * 备注：服务的开关、端口与令牌由首选项的 debug.* 设置项决定（或启动参数），不经本接口修改；数据类型由 schema/debug.schema.json 生成。
 * ------------------------------------------------------------------------
 */

#pragma once

#include "contracts/debug/generated/debug.hpp"

namespace ember::contracts::debug {

/** 调试服务。 */
class DebugService {
 public:
  virtual ~DebugService() = default;

  /** 当前状态：是否开启、是否在监听、端口与连接；可在任意线程调用。 */
  [[nodiscard]] virtual DebugStatusResult status() const = 0;
};

}  // namespace ember::contracts::debug
