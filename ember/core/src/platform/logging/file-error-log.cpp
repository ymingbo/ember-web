/*
 * ------------------------------------------------------------------------
 * 名称：file-error-log.cpp
 * 说明：文件错误日志的实现。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-13
 * ------------------------------------------------------------------------
 */

#include "platform/logging/file-error-log.hpp"

#include <fstream>
#include <system_error>

namespace ember::platform {

FileErrorLog::FileErrorLog(std::filesystem::path logsRoot, std::shared_ptr<const Clock> clock)
    : logsRoot_(std::move(logsRoot)), clock_(std::move(clock)), writer_([this] { run(); }) {}

FileErrorLog::~FileErrorLog() {
  {
    std::lock_guard lock(mutex_);
    stopping_ = true;
  }
  ready_.notify_all();
  if (writer_.joinable()) writer_.join();
}

void FileErrorLog::write(const ErrorLogEntry& entry) noexcept {
  try {
    const auto time = clock_->now();
    nlohmann::json line = {
        {"time", formatUtcIso(time)},
        {"process", "core"},
        {"module", entry.module.empty() ? nlohmann::json(nullptr) : nlohmann::json(entry.module)},
        {"code", entry.code},
        {"message", entry.message},
        {"stack", nullptr},
        {"context", entry.context},
    };
    Line item{formatLocalDate(time), line.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace)};
    {
      std::lock_guard lock(mutex_);
      if (stopping_) return;
      lines_.push_back(std::move(item));
    }
    ready_.notify_one();
  } catch (...) {
    // 写日志失败不影响业务，也不再写日志。
  }
}

void FileErrorLog::run() {
  for (;;) {
    Line line;
    {
      std::unique_lock lock(mutex_);
      ready_.wait(lock, [this] { return stopping_ || !lines_.empty(); });
      if (lines_.empty()) return;
      line = std::move(lines_.front());
      lines_.pop_front();
    }
    try {
      const auto directory = logsRoot_ / line.date;
      std::error_code error;
      std::filesystem::create_directories(directory, error);
      std::ofstream file(directory / "errors.jsonl", std::ios::app | std::ios::binary);
      file << line.text << '\n';
    } catch (...) {
      // 丢弃写不进去的一条。
    }
  }
}

}  // namespace ember::platform
