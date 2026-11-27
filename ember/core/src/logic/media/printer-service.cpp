/*
 * ------------------------------------------------------------------------
 * 名称：printer-service.cpp
 * 说明：打印机输出的接收与记录的实现。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-15
 * ------------------------------------------------------------------------
 */

#include "logic/media/printer-service.hpp"

#include <ctime>
#include <utility>

#include "contracts/media/generated/media.hpp"
#include "shared/errors/core-error.hpp"

namespace ember::logic::media {

namespace api = contracts::media;

PrinterService::PrinterService(std::shared_ptr<api::PrinterPageStore> store, std::function<std::string()> directory, std::shared_ptr<platform::EventSink> events, std::shared_ptr<platform::ErrorLog> errors, TimeSource now)
    : store_(std::move(store)), directory_(std::move(directory)), events_(std::move(events)), errors_(std::move(errors)), now_(std::move(now)) {}

std::string PrinterService::baseName() const {
  const std::time_t seconds = std::chrono::system_clock::to_time_t(now_());
  std::tm local{};
#ifdef _WIN32
  localtime_s(&local, &seconds);
#else
  localtime_r(&seconds, &local);
#endif
  char text[32];
  std::strftime(text, sizeof(text), "printer-%Y%m%d-%H%M%S", &local);
  return text;
}

void PrinterService::outputPage(contracts::hardware::PrintedPage page) {
  try {
    const std::string fileName = store_->save(directory_(), baseName(), page);
    std::int64_t number = 0;
    {
      std::lock_guard lock(mutex_);
      number = ++record_.pages;
      record_.lastFile = fileName;
    }
    events_->publish(api::MEDIA_PRINTED_EVENT, nlohmann::json(api::MediaPrintedData{fileName, number}));
  } catch (const shared::CoreError& error) {
    // 这一页丢弃；原因经事件交给界面，同时写进错误日志。
    errors_->write({"media", error.code(), error.what(), {}});
    events_->publish(api::MEDIA_PRINT_FAILED_EVENT, nlohmann::json(api::MediaPrintFailedData{error.what()}));
  }
}

api::PrinterRecordSnapshot PrinterService::snapshot() const {
  api::PrinterRecordSnapshot copy;
  {
    std::lock_guard lock(mutex_);
    copy = record_;
  }
  copy.directory = directory_();
  return copy;
}

}  // namespace ember::logic::media
