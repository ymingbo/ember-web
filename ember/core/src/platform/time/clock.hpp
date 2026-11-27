/*
 * ------------------------------------------------------------------------
 * 名称：clock.hpp
 * 说明：时钟抽象，以及本地日期、本地日期时间、UTC 时间文本的格式化与 UTC 时间文本的解析；业务代码经抽象取时间，测试可替换，系统实现见 system-clock.hpp。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-13
 * 备注：时间文本使用 UTC 的 ISO 8601（带毫秒）；本地日期用于日志目录名。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <chrono>
#include <cstdio>
#include <ctime>
#include <optional>
#include <string>
#include <string_view>

namespace ember::platform {

/** 时钟。 */
class Clock {
 public:
  virtual ~Clock() = default;

  /** 当前的系统时间。 */
  [[nodiscard]] virtual std::chrono::system_clock::time_point now() const = 0;

  /** 单调时间，用于计算耗时。 */
  [[nodiscard]] virtual std::chrono::steady_clock::time_point steadyNow() const = 0;
};

/** 把时间点拆成本地或 UTC 的日历时间。 */
inline std::tm toCalendar(std::chrono::system_clock::time_point time, bool local) {
  const std::time_t seconds = std::chrono::system_clock::to_time_t(time);
  std::tm calendar{};
#if defined(_WIN32)
  if (local) localtime_s(&calendar, &seconds);
  else gmtime_s(&calendar, &seconds);
#else
  if (local) localtime_r(&seconds, &calendar);
  else gmtime_r(&seconds, &calendar);
#endif
  return calendar;
}

/** 本地日期，如 2026-11-03。 */
inline std::string formatLocalDate(std::chrono::system_clock::time_point time) {
  const std::tm calendar = toCalendar(time, true);
  char text[16]{};
  std::strftime(text, sizeof(text), "%Y-%m-%d", &calendar);
  return text;
}

/** 本地日期与时间，用于文件名，如 20261103-093015。 */
inline std::string formatLocalCompact(std::chrono::system_clock::time_point time) {
  const std::tm calendar = toCalendar(time, true);
  char text[24]{};
  std::strftime(text, sizeof(text), "%Y%m%d-%H%M%S", &calendar);
  return text;
}

/** UTC 的 ISO 8601 时间，如 2026-11-03T01:30:15.482Z。 */
inline std::string formatUtcIso(std::chrono::system_clock::time_point time) {
  const std::tm calendar = toCalendar(time, false);
  char text[32]{};
  std::strftime(text, sizeof(text), "%Y-%m-%dT%H:%M:%S", &calendar);
  const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(time.time_since_epoch()).count() % 1000;
  char result[40]{};
  std::snprintf(result, sizeof(result), "%s.%03dZ", text, static_cast<int>(milliseconds < 0 ? milliseconds + 1000 : milliseconds));
  return result;
}

/** 本地日期与时间，带毫秒，如 2026-11-03 09:30:15.482；文本按时间先后排序。 */
inline std::string formatLocalDateTime(std::chrono::system_clock::time_point time) {
  const std::tm calendar = toCalendar(time, true);
  char text[32]{};
  std::strftime(text, sizeof(text), "%Y-%m-%d %H:%M:%S", &calendar);
  const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(time.time_since_epoch()).count() % 1000;
  char result[40]{};
  std::snprintf(result, sizeof(result), "%s.%03d", text, static_cast<int>(milliseconds < 0 ? milliseconds + 1000 : milliseconds));
  return result;
}

/** 解析 formatUtcIso 写出的时间文本（毫秒可省略，末尾的 Z 可省略）；格式不对时返回空。 */
inline std::optional<std::chrono::system_clock::time_point> parseUtcIso(std::string_view text) {
  // 按固定位置读取数字：年 4 位，其余各 2 位，分隔符固定。
  const auto digits = [&text](std::size_t start, std::size_t count) -> int {
    int value = 0;
    if (start + count > text.size()) return -1;
    for (std::size_t index = start; index < start + count; ++index) {
      if (text[index] < '0' || text[index] > '9') return -1;
      value = value * 10 + (text[index] - '0');
    }
    return value;
  };
  if (text.size() < 19 || text[4] != '-' || text[7] != '-' || text[10] != 'T' || text[13] != ':' || text[16] != ':') return std::nullopt;
  const int year = digits(0, 4);
  const int month = digits(5, 2);
  const int day = digits(8, 2);
  const int hour = digits(11, 2);
  const int minute = digits(14, 2);
  const int second = digits(17, 2);
  if (year < 1970 || month < 1 || month > 12 || day < 1 || day > 31 || hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 60) return std::nullopt;
  int milliseconds = 0;
  if (text.size() > 19 && text[19] == '.') {
    // 毫秒取小数点后的前 3 位，不足 3 位时补零。
    int scale = 100;
    for (std::size_t index = 20; index < text.size() && text[index] >= '0' && text[index] <= '9' && scale > 0; ++index, scale /= 10) milliseconds += (text[index] - '0') * scale;
  }
  std::tm calendar{};
  calendar.tm_year = year - 1900;
  calendar.tm_mon = month - 1;
  calendar.tm_mday = day;
  calendar.tm_hour = hour;
  calendar.tm_min = minute;
  calendar.tm_sec = second;
#if defined(_WIN32)
  const std::time_t seconds = _mkgmtime(&calendar);
#else
  const std::time_t seconds = timegm(&calendar);
#endif
  if (seconds == static_cast<std::time_t>(-1)) return std::nullopt;
  return std::chrono::system_clock::from_time_t(seconds) + std::chrono::milliseconds(milliseconds);
}

}  // namespace ember::platform
