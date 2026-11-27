/*
 * ------------------------------------------------------------------------
 * 名称：error-log.hpp
 * 说明：系统错误日志的抽象：核心内的意外错误经它写入，不直接操作日志文件。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-13
 * 备注：字段与主进程的 file-error-logger.ts 一致；写日志失败不影响业务，也不再写日志。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <string>

#include <nlohmann/json.hpp>

namespace ember::platform {

/** 一条系统错误。 */
struct ErrorLogEntry {
  /** 所属模块，无法确定时为空。 */
  std::string module;
  /** 错误码。 */
  std::string code;
  /** 说明。 */
  std::string message;
  /** 排查需要的上下文，如请求名、请求标识。 */
  nlohmann::json context = nlohmann::json::object();
};

/** 系统错误日志。 */
class ErrorLog {
 public:
  virtual ~ErrorLog() = default;

  /** 写入一条错误；不抛出异常。 */
  virtual void write(const ErrorLogEntry& entry) noexcept = 0;
};

}  // namespace ember::platform
