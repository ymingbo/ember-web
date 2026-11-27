/*
 * ------------------------------------------------------------------------
 * 名称：file-error-log.hpp
 * 说明：系统错误日志的文件实现：在后台线程把错误追加到 <日志目录>/<本地日期>/errors.jsonl，一行一条。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-13
 * 备注：析构时写完已排队的错误再退出；写入失败时丢弃该条，不再写日志。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <condition_variable>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "platform/logging/error-log.hpp"
#include "platform/time/clock.hpp"

namespace ember::platform {

/** 文件错误日志。 */
class FileErrorLog final : public ErrorLog {
 public:
  /**
   * @param logsRoot 日志根目录（<用户数据目录>/logs）。
   * @param clock 时钟。
   */
  FileErrorLog(std::filesystem::path logsRoot, std::shared_ptr<const Clock> clock);
  ~FileErrorLog() override;

  FileErrorLog(const FileErrorLog&) = delete;
  FileErrorLog& operator=(const FileErrorLog&) = delete;

  void write(const ErrorLogEntry& entry) noexcept override;

 private:
  struct Line {
    std::string date;
    std::string text;
  };

  void run();

  std::filesystem::path logsRoot_;
  std::shared_ptr<const Clock> clock_;
  std::mutex mutex_;
  std::condition_variable ready_;
  std::deque<Line> lines_;
  bool stopping_ = false;
  std::thread writer_;
};

}  // namespace ember::platform
