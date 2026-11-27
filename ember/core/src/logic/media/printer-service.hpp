/*
 * ------------------------------------------------------------------------
 * 名称：printer-service.hpp
 * 说明：打印机输出的接收：虚拟打印机输出一页时，按首选项里的保存位置存成 PNG 文件，发 media.printed；保存失败时丢弃这一页，发 media.printFailed 并写错误日志。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-15
 * 备注：文件名是 printer-YYYYMMDD-HHMMSS（按输出时的本机日期时间）加扩展名，同一秒输出多页时由保存实现加 -2、-3……；
 *       本对象在模拟线程上被调用，记录（页数、最近的文件名）可以在请求线程上读取。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

#include "contracts/hardware/printer-output.hpp"
#include "contracts/media/printer-page-store.hpp"
#include "contracts/media/printer-record.hpp"
#include "platform/events/event-sink.hpp"
#include "platform/logging/error-log.hpp"

namespace ember::logic::media {

/** 打印机输出的接收与记录。 */
class PrinterService final : public contracts::hardware::PrinterOutput, public contracts::media::PrinterRecord {
 public:
  /** 取当前时间。 */
  using TimeSource = std::function<std::chrono::system_clock::time_point()>;

  /**
   * @param store 保存实现。
   * @param directory 取保存目录（UTF-8）的函数，每输出一页调用一次，所以首选项改了立即生效。
   * @param events 事件发布。
   * @param errors 错误日志。
   * @param now 取当前时间的函数；默认取系统时间。
   */
  PrinterService(std::shared_ptr<contracts::media::PrinterPageStore> store, std::function<std::string()> directory, std::shared_ptr<platform::EventSink> events, std::shared_ptr<platform::ErrorLog> errors,
                 TimeSource now = [] { return std::chrono::system_clock::now(); });

  void outputPage(contracts::hardware::PrintedPage page) override;
  [[nodiscard]] contracts::media::PrinterRecordSnapshot snapshot() const override;

 private:
  /** 本机日期时间组成的文件名（不含扩展名）：printer-YYYYMMDD-HHMMSS。 */
  [[nodiscard]] std::string baseName() const;

  std::shared_ptr<contracts::media::PrinterPageStore> store_;
  std::function<std::string()> directory_;
  std::shared_ptr<platform::EventSink> events_;
  std::shared_ptr<platform::ErrorLog> errors_;
  TimeSource now_;

  mutable std::mutex mutex_;
  contracts::media::PrinterRecordSnapshot record_;
};

}  // namespace ember::logic::media
