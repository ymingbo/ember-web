/*
 * ------------------------------------------------------------------------
 * 名称：disk-text.cpp
 * 说明：磁盘镜像文件系统用的小工具的实现。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-25
 * 备注：无。
 * ------------------------------------------------------------------------
 */

#include "logic/filemanager/disk-text.hpp"

#include <array>
#include <cstdio>
#include <ctime>

namespace ember::logic::filemanager {

namespace {

/** CP437 的 0x80 到 0xFF 对应的 Unicode 码点。 */
constexpr std::array<char32_t, 128> CP437_HIGH = {
    0x00C7, 0x00FC, 0x00E9, 0x00E2, 0x00E4, 0x00E0, 0x00E5, 0x00E7, 0x00EA, 0x00EB, 0x00E8, 0x00EF, 0x00EE, 0x00EC, 0x00C4, 0x00C5,
    0x00C9, 0x00E6, 0x00C6, 0x00F4, 0x00F6, 0x00F2, 0x00FB, 0x00F9, 0x00FF, 0x00D6, 0x00DC, 0x00A2, 0x00A3, 0x00A5, 0x20A7, 0x0192,
    0x00E1, 0x00ED, 0x00F3, 0x00FA, 0x00F1, 0x00D1, 0x00AA, 0x00BA, 0x00BF, 0x2310, 0x00AC, 0x00BD, 0x00BC, 0x00A1, 0x00AB, 0x00BB,
    0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556, 0x2555, 0x2563, 0x2551, 0x2557, 0x255D, 0x255C, 0x255B, 0x2510,
    0x2514, 0x2534, 0x252C, 0x251C, 0x2500, 0x253C, 0x255E, 0x255F, 0x255A, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256C, 0x2567,
    0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256B, 0x256A, 0x2518, 0x250C, 0x2588, 0x2584, 0x258C, 0x2590, 0x2580,
    0x03B1, 0x00DF, 0x0393, 0x03C0, 0x03A3, 0x03C3, 0x00B5, 0x03C4, 0x03A6, 0x0398, 0x03A9, 0x03B4, 0x221E, 0x03C6, 0x03B5, 0x2229,
    0x2261, 0x00B1, 0x2265, 0x2264, 0x2320, 0x2321, 0x00F7, 0x2248, 0x00B0, 0x2219, 0x00B7, 0x221A, 0x207F, 0x00B2, 0x25A0, 0x00A0};

/** 码点追加成 UTF-8（码点都在基本平面内）。 */
void appendUtf8(std::string& out, char32_t codePoint) {
  if (codePoint < 0x80) {
    out.push_back(static_cast<char>(codePoint));
  } else if (codePoint < 0x800) {
    out.push_back(static_cast<char>(0xC0 | (codePoint >> 6)));
    out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
  } else {
    out.push_back(static_cast<char>(0xE0 | (codePoint >> 12)));
    out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
  }
}

char lowerChar(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }

}  // namespace

bool isPrintableAscii(std::string_view text) {
  for (const char c : text) {
    const auto byte = static_cast<unsigned char>(c);
    if (byte < 0x20 || byte > 0x7E) return false;
  }
  return true;
}

std::string upperAscii(std::string_view text) {
  std::string result(text);
  for (char& c : result) {
    if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
  }
  return result;
}

std::string lowerAscii(std::string_view text) {
  std::string result(text);
  for (char& c : result) c = lowerChar(c);
  return result;
}

bool equalsIgnoreCase(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) return false;
  for (std::size_t i = 0; i < a.size(); ++i) {
    if (lowerChar(a[i]) != lowerChar(b[i])) return false;
  }
  return true;
}

std::string oemToUtf8(std::string_view bytes) {
  std::string result;
  result.reserve(bytes.size());
  for (const char c : bytes) {
    const auto byte = static_cast<unsigned char>(c);
    if (byte >= 0x80) appendUtf8(result, CP437_HIGH[byte - 0x80]);
    else if (byte < 0x20 || byte == 0x7F) result.push_back('?');
    else result.push_back(c);
  }
  return result;
}

LocalTime currentLocalTime() {
  const std::time_t now = std::time(nullptr);
  std::tm parts{};
#ifdef _WIN32
  localtime_s(&parts, &now);
#else
  localtime_r(&now, &parts);
#endif
  LocalTime result;
  result.year = parts.tm_year + 1900;
  result.month = parts.tm_mon + 1;
  result.day = parts.tm_mday;
  result.hour = parts.tm_hour;
  result.minute = parts.tm_min;
  result.second = parts.tm_sec;
  return result;
}

std::string formatTime(const LocalTime& time, bool withSeconds) {
  std::array<char, 32> text{};
  if (withSeconds) std::snprintf(text.data(), text.size(), "%04d-%02d-%02d %02d:%02d:%02d", time.year, time.month, time.day, time.hour, time.minute, time.second);
  else std::snprintf(text.data(), text.size(), "%04d-%02d-%02d %02d:%02d", time.year, time.month, time.day, time.hour, time.minute);
  return text.data();
}

std::string base64Encode(const std::vector<std::uint8_t>& bytes) {
  static constexpr char ALPHABET[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve((bytes.size() + 2) / 3 * 4);
  for (std::size_t i = 0; i < bytes.size(); i += 3) {
    const std::size_t left = bytes.size() - i;
    const std::uint32_t group = (static_cast<std::uint32_t>(bytes[i]) << 16) | ((left > 1 ? static_cast<std::uint32_t>(bytes[i + 1]) : 0U) << 8) | (left > 2 ? bytes[i + 2] : 0U);
    out.push_back(ALPHABET[(group >> 18) & 0x3FU]);
    out.push_back(ALPHABET[(group >> 12) & 0x3FU]);
    out.push_back(left > 1 ? ALPHABET[(group >> 6) & 0x3FU] : '=');
    out.push_back(left > 2 ? ALPHABET[group & 0x3FU] : '=');
  }
  return out;
}

std::string windowsSafeName(std::string name) {
  for (char& c : name) {
    if (static_cast<unsigned char>(c) < 0x20 || std::string_view("<>:\"/\\|?*").find(c) != std::string_view::npos) c = '_';
  }
  while (!name.empty() && (name.back() == '.' || name.back() == ' ')) name.pop_back();
  return name.empty() ? std::string("_") : name;
}

}  // namespace ember::logic::filemanager
