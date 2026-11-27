/*
 * ------------------------------------------------------------------------
 * 名称：disk-text.hpp
 * 说明：磁盘镜像文件系统用的小工具：小端字节读写、ASCII 大小写、CP437 显示、当前本地时间与时间文字。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-25
 * 备注：磁盘上的名称只有 ASCII 部分按文字处理；FAT 名称里 0x80 以上的字节按 CP437 换成 Unicode 显示（保证界面收到的是合法的 UTF-8），写入时名称只能是 ASCII。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ember::logic::filemanager {

/** 读 16 位小端整数。 */
[[nodiscard]] inline std::uint16_t readLe16(const std::uint8_t* bytes) { return static_cast<std::uint16_t>(bytes[0] | (bytes[1] << 8)); }

/** 读 32 位小端整数。 */
[[nodiscard]] inline std::uint32_t readLe32(const std::uint8_t* bytes) {
  return static_cast<std::uint32_t>(bytes[0]) | (static_cast<std::uint32_t>(bytes[1]) << 8) | (static_cast<std::uint32_t>(bytes[2]) << 16) | (static_cast<std::uint32_t>(bytes[3]) << 24);
}

/** 读 24 位小端整数（ProDOS 的文件长度）。 */
[[nodiscard]] inline std::uint32_t readLe24(const std::uint8_t* bytes) {
  return static_cast<std::uint32_t>(bytes[0]) | (static_cast<std::uint32_t>(bytes[1]) << 8) | (static_cast<std::uint32_t>(bytes[2]) << 16);
}

/** 写 16 位小端整数。 */
inline void writeLe16(std::uint8_t* bytes, std::uint32_t value) {
  bytes[0] = static_cast<std::uint8_t>(value & 0xFFU);
  bytes[1] = static_cast<std::uint8_t>((value >> 8) & 0xFFU);
}

/** 写 32 位小端整数。 */
inline void writeLe32(std::uint8_t* bytes, std::uint32_t value) {
  writeLe16(bytes, value & 0xFFFFU);
  writeLe16(bytes + 2, value >> 16);
}

/** 写 24 位小端整数。 */
inline void writeLe24(std::uint8_t* bytes, std::uint32_t value) {
  writeLe16(bytes, value & 0xFFFFU);
  bytes[2] = static_cast<std::uint8_t>((value >> 16) & 0xFFU);
}

/** 文字是否全是 ASCII 可见字符与空格（0x20 到 0x7E）。 */
[[nodiscard]] bool isPrintableAscii(std::string_view text);

/** ASCII 字母转大写，其余字符不变。 */
[[nodiscard]] std::string upperAscii(std::string_view text);

/** ASCII 字母转小写，其余字符不变。 */
[[nodiscard]] std::string lowerAscii(std::string_view text);

/** 两段文字是否相同，ASCII 字母不分大小写。 */
[[nodiscard]] bool equalsIgnoreCase(std::string_view a, std::string_view b);

/** 磁盘上的名称字节换成 UTF-8：0x80 以上按 CP437，0x20 以下换成问号。 */
[[nodiscard]] std::string oemToUtf8(std::string_view bytes);

/** 本地时间的各个字段。 */
struct LocalTime {
  int year{1980};
  int month{1};
  int day{1};
  int hour{0};
  int minute{0};
  int second{0};
};

/** 现在的本地时间。 */
[[nodiscard]] LocalTime currentLocalTime();

/** 字节编码成 Base64 文字（带补位的 =）。 */
[[nodiscard]] std::string base64Encode(const std::vector<std::uint8_t>& bytes);

/** 把磁盘里的名称改成 Windows 文件名：不能用的字符换成下划线，去掉末尾的点与空格；结果不会为空。 */
[[nodiscard]] std::string windowsSafeName(std::string name);

/** 时间写成 2026-11-25 14:30；withSeconds 为真时带秒。 */
[[nodiscard]] std::string formatTime(const LocalTime& time, bool withSeconds);

}  // namespace ember::logic::filemanager
