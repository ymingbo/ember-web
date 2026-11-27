/*
 * ------------------------------------------------------------------------
 * 名称：emap-package.cpp
 * 说明：mapper 插件包的读取与检查实现，用 miniz 读取 zip。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * ------------------------------------------------------------------------
 */

#include "infra/mappers/emap-package.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <optional>
#include <set>
#include <vector>

#include <miniz.h>

#include "contracts/mappers/mapper-error.hpp"
#include "shared/errors/core-error.hpp"

namespace ember::infra::mappers {

namespace {

constexpr const char* MANIFEST_NAME = "manifest.json";
constexpr const char* SCRIPT_NAME = "main.js";
constexpr const char* ICON_NAME = "icon.png";
/** 包里最多检查的条目数：超过它一定包含不允许的内容。 */
constexpr mz_uint MAX_ENTRIES = 16;
/** zip 条目的外部属性：高 16 位是 Unix 的文件类型与权限，0xA000 为符号链接。 */
constexpr mz_uint32 UNIX_TYPE_MASK = 0xF000;
constexpr mz_uint32 UNIX_SYMLINK = 0xA000;
constexpr mz_uint16 MADE_BY_UNIX = 3;

[[noreturn]] void fail(const std::string& message) { throw shared::CoreError(contracts::mappers::ERROR_INVALID_PACKAGE, "插件包不合格：" + message); }

/** 离开作用域时关闭 zip 读取器。 */
class ZipReaderGuard {
 public:
  explicit ZipReaderGuard(mz_zip_archive& zip) : zip_(zip) {}
  ~ZipReaderGuard() { mz_zip_reader_end(&zip_); }
  ZipReaderGuard(const ZipReaderGuard&) = delete;
  ZipReaderGuard& operator=(const ZipReaderGuard&) = delete;

 private:
  mz_zip_archive& zip_;
};

/** 文件名是否会跳出包的根目录：绝对路径、盘符、反斜杠、“..” 段。 */
bool isUnsafeName(const std::string& name) {
  if (name.empty() || name.front() == '/' || name.find('\\') != std::string::npos || name.find(':') != std::string::npos) return true;
  std::size_t begin = 0;
  while (begin <= name.size()) {
    const std::size_t end = name.find('/', begin);
    const std::string segment = name.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
    if (segment == "..") return true;
    if (end == std::string::npos) break;
    begin = end + 1;
  }
  return false;
}

/** 单个文件解压后的大小上限。 */
std::size_t limitOf(const std::string& name) {
  if (name == MANIFEST_NAME) return MAX_MANIFEST_BYTES;
  if (name == SCRIPT_NAME) return MAX_SCRIPT_BYTES;
  return MAX_PACKAGE_BYTES;
}

/** 图标必须是正方形的 PNG，边长不小于 MIN_ICON_SIZE。 */
void checkIcon(const std::vector<char>& data) {
  static constexpr unsigned char SIGNATURE[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
  if (data.size() < 24 || std::memcmp(data.data(), SIGNATURE, sizeof(SIGNATURE)) != 0) fail("icon.png 不是 PNG 图片");
  const auto read32 = [&data](std::size_t offset) {
    std::uint32_t value = 0;
    for (std::size_t index = 0; index < 4; ++index) value = (value << 8) | static_cast<unsigned char>(data[offset + index]);
    return value;
  };
  const std::uint32_t width = read32(16);
  const std::uint32_t height = read32(20);
  if (width != height || width < MIN_ICON_SIZE) fail("icon.png 必须是正方形，边长不小于 " + std::to_string(MIN_ICON_SIZE) + " 像素");
}

}  // namespace

EmapPackage readEmapDirectory(const std::filesystem::path& directory) {
  namespace fs = std::filesystem;
  // 读目录里的一个文件；不存在返回空，不是普通文件、太大或读不全时报错。
  const auto readOne = [&directory](const char* name, std::size_t limit) -> std::optional<std::vector<char>> {
    const fs::path file = directory / name;
    std::error_code error;
    const fs::file_status status = fs::symlink_status(file, error);
    if (error || status.type() == fs::file_type::not_found) return std::nullopt;
    if (status.type() != fs::file_type::regular) fail(std::string(name) + " 不是普通文件（不允许目录与符号链接）");
    const auto size = fs::file_size(file, error);
    if (error) fail(std::string("无法读取 ") + name);
    if (size > limit) fail(std::string(name) + " 超过大小上限");
    std::ifstream stream(file, std::ios::binary);
    std::vector<char> data(static_cast<std::size_t>(size));
    stream.read(data.data(), static_cast<std::streamsize>(data.size()));
    if (!stream || static_cast<std::size_t>(stream.gcount()) != data.size()) fail(std::string("读取 ") + name + " 时文件被截断");
    return data;
  };
  EmapPackage package;
  const auto manifest = readOne(MANIFEST_NAME, MAX_MANIFEST_BYTES);
  if (!manifest.has_value()) fail("缺少 manifest.json");
  package.manifestText.assign(manifest->begin(), manifest->end());
  if (const auto script = readOne(SCRIPT_NAME, MAX_SCRIPT_BYTES)) {
    package.hasScript = true;
    package.scriptText.assign(script->begin(), script->end());
  }
  if (const auto icon = readOne(ICON_NAME, MAX_PACKAGE_BYTES)) {
    checkIcon(*icon);
    package.hasIcon = true;
  }
  return package;
}

EmapPackage readEmapPackage(std::span<const std::uint8_t> bytes) {
  if (bytes.empty()) fail("文件是空的");
  if (bytes.size() > MAX_PACKAGE_BYTES) fail("文件超过 " + std::to_string(MAX_PACKAGE_BYTES / 1024 / 1024) + " MB 的上限");

  mz_zip_archive zip{};
  if (mz_zip_reader_init_mem(&zip, bytes.data(), bytes.size(), 0) == MZ_FALSE) {
    fail(std::string("不是有效的 zip 文件（") + mz_zip_get_error_string(mz_zip_get_last_error(&zip)) + "）");
  }
  const ZipReaderGuard guard(zip);

  const mz_uint count = mz_zip_reader_get_num_files(&zip);
  if (count == 0) fail("包里没有文件");
  if (count > MAX_ENTRIES) fail("包里的文件过多，只允许 manifest.json、main.js 与 icon.png");

  EmapPackage package;
  std::set<std::string> seen;
  bool hasManifest = false;
  for (mz_uint index = 0; index < count; ++index) {
    mz_zip_archive_file_stat stat{};
    if (mz_zip_reader_file_stat(&zip, index, &stat) == MZ_FALSE) fail("无法读取包的文件目录");
    const std::string name = stat.m_filename;
    if (isUnsafeName(name)) fail("文件名不安全：" + name);
    if (stat.m_is_directory != MZ_FALSE || name.back() == '/') fail("包里不允许出现目录：" + name);
    if (name != MANIFEST_NAME && name != SCRIPT_NAME && name != ICON_NAME) fail("包里不允许出现其他文件：" + name + "（只允许 manifest.json、main.js 与 icon.png）");
    if (!seen.insert(name).second) fail("文件重复：" + name);
    if (stat.m_version_made_by >> 8 == MADE_BY_UNIX && ((stat.m_external_attr >> 16) & UNIX_TYPE_MASK) == UNIX_SYMLINK) fail("不允许符号链接：" + name);
    if (stat.m_is_encrypted != MZ_FALSE || stat.m_is_supported == MZ_FALSE) fail("文件已加密或使用了不支持的压缩方式：" + name);
    if (stat.m_uncomp_size > limitOf(name)) fail(name + " 解压后超过大小上限");

    std::vector<char> data(static_cast<std::size_t>(stat.m_uncomp_size));
    if (!data.empty() && mz_zip_reader_extract_to_mem(&zip, index, data.data(), data.size(), 0) == MZ_FALSE) fail("文件损坏或解压失败：" + name);
    if (name == MANIFEST_NAME) {
      hasManifest = true;
      package.manifestText.assign(data.begin(), data.end());
    } else if (name == SCRIPT_NAME) {
      package.hasScript = true;
      package.scriptText.assign(data.begin(), data.end());
    } else {
      checkIcon(data);
      package.hasIcon = true;
    }
  }
  if (!hasManifest) fail("缺少 manifest.json");
  return package;
}

}  // namespace ember::infra::mappers
