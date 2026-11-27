/*
 * ------------------------------------------------------------------------
 * 名称：disk-rom-storage.cpp
 * 说明：ROM 文件存取的磁盘实现。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-15
 * ------------------------------------------------------------------------
 */

#include "infra/machines/disk-rom-storage.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <string>

#include "contracts/machines/machines-error.hpp"
#include "shared/errors/core-error.hpp"

namespace ember::infra::machines {

namespace api = contracts::machines;
namespace fs = std::filesystem;

namespace {

/** 保存过程中的临时文件的扩展名。 */
constexpr const char* TEMPORARY_SUFFIX = ".tmp";

/** 名字只能是一个普通的名字：不含路径分隔符、不是 . 或 ..。 */
void requirePlainName(const std::string& name) {
  if (name.empty() || name == "." || name == ".." || name.find_first_of("/\\:") != std::string::npos) {
    throw shared::CoreError(api::ERROR_ROM_ACCESS_FAILED, "不合法的名字：" + name);
  }
}

fs::path toPath(const std::string& text) { return fs::path(std::u8string(text.begin(), text.end())); }

std::string fileNameOf(const fs::path& path) {
  const std::u8string name = path.filename().u8string();
  return std::string(name.begin(), name.end());
}

[[noreturn]] void accessFailed(const std::string& what, const fs::path& path) { throw shared::CoreError(api::ERROR_ROM_ACCESS_FAILED, what + "：" + fileNameOf(path)); }

}  // namespace

std::vector<std::uint8_t> DiskRomStorage::readSource(const std::string& path) const {
  const fs::path file = toPath(path);
  std::error_code error;
  const auto status = fs::status(file, error);
  if (error || !fs::exists(status)) throw shared::CoreError(api::ERROR_FILE_NOT_FOUND, "找不到文件：" + path);
  if (!fs::is_regular_file(status)) throw shared::CoreError(api::ERROR_FILE_UNREADABLE, "不是普通文件：" + path);
  const auto size = fs::file_size(file, error);
  if (error) throw shared::CoreError(api::ERROR_FILE_UNREADABLE, "无法读取文件：" + path);
  if (size > MAX_FILE_SIZE) throw shared::CoreError(api::ERROR_ROM_INVALID, "文件超过 16 MB，不是 ROM：" + path);

  std::ifstream stream(file, std::ios::binary);
  if (!stream) throw shared::CoreError(api::ERROR_FILE_UNREADABLE, "无法打开文件：" + path);
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
  stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  if (stream.gcount() != static_cast<std::streamsize>(bytes.size())) throw shared::CoreError(api::ERROR_FILE_UNREADABLE, "读取文件时中断：" + path);
  return bytes;
}

std::vector<std::string> DiskRomStorage::listSources(const std::string& directory) const {
  const fs::path folder = toPath(directory);
  std::error_code error;
  const auto status = fs::status(folder, error);
  if (error || !fs::exists(status)) throw shared::CoreError(api::ERROR_FILE_NOT_FOUND, "找不到目录：" + directory);
  if (!fs::is_directory(status)) throw shared::CoreError(api::ERROR_FILE_UNREADABLE, "不是目录：" + directory);

  std::vector<fs::path> files;
  for (fs::directory_iterator item(folder, error), end; !error && item != end; item.increment(error)) {
    std::error_code entryError;
    if (!item->is_regular_file(entryError) || entryError) continue;
    std::string extension = fileNameOf(item->path().extension());
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char letter) { return static_cast<char>(std::tolower(letter)); });
    if (extension == ".bin") files.push_back(item->path());
  }
  if (error) throw shared::CoreError(api::ERROR_FILE_UNREADABLE, "无法读取目录：" + directory);
  std::sort(files.begin(), files.end());

  std::vector<std::string> result;
  for (const fs::path& file : files) {
    const std::u8string text = file.u8string();
    result.emplace_back(text.begin(), text.end());
  }
  return result;
}

fs::path DiskRomStorage::directoryOf(const std::string& machine) const {
  requirePlainName(machine);
  return root_ / toPath(machine);
}

fs::path DiskRomStorage::storedFile(const std::string& machine) const {
  const fs::path directory = directoryOf(machine);
  std::error_code error;
  if (!fs::is_directory(directory, error)) return {};
  for (const fs::directory_entry& item : fs::directory_iterator(directory, error)) {
    if (error) break;
    if (item.is_regular_file(error) && item.path().extension() != TEMPORARY_SUFFIX) return item.path();
  }
  return {};
}

std::optional<api::StoredRom> DiskRomStorage::find(const std::string& machine) const {
  const fs::path file = storedFile(machine);
  if (file.empty()) return std::nullopt;
  std::error_code error;
  const auto size = fs::file_size(file, error);
  if (error) return std::nullopt;
  return api::StoredRom{fileNameOf(file), static_cast<std::int64_t>(size)};
}

std::vector<std::uint8_t> DiskRomStorage::load(const std::string& machine) const {
  const fs::path file = storedFile(machine);
  if (file.empty()) throw shared::CoreError(api::ERROR_NO_ROM, "这个机型还没有添加 ROM");
  std::ifstream stream(file, std::ios::binary);
  if (!stream) accessFailed("无法打开 ROM 文件", file);
  std::error_code error;
  const auto size = fs::file_size(file, error);
  if (error) accessFailed("无法读取 ROM 文件", file);
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
  stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  if (stream.gcount() != static_cast<std::streamsize>(bytes.size())) accessFailed("读取 ROM 文件时中断", file);
  return bytes;
}

void DiskRomStorage::store(const std::string& machine, const std::string& fileName, const std::vector<std::uint8_t>& bytes) {
  const fs::path directory = directoryOf(machine);
  requirePlainName(fileName);
  const fs::path target = directory / toPath(fileName);
  if (target.extension() == TEMPORARY_SUFFIX) throw shared::CoreError(api::ERROR_ROM_ACCESS_FAILED, "不合法的文件名：" + fileName);
  std::error_code error;
  fs::create_directories(directory, error);
  if (error) accessFailed("无法创建 ROM 目录", directory);

  // 先写临时文件，再改名；改名成功之后才删其他文件。
  fs::path temporary = target;
  temporary += TEMPORARY_SUFFIX;
  {
    std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
    stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    stream.flush();
    if (!stream) {
      stream.close();
      fs::remove(temporary, error);
      accessFailed("无法写入 ROM 文件", target);
    }
  }
  fs::rename(temporary, target, error);
  if (error) {
    fs::remove(temporary, error);
    accessFailed("无法保存 ROM 文件", target);
  }
  for (const fs::directory_entry& item : fs::directory_iterator(directory, error)) {
    if (error) break;
    if (item.path() != target) fs::remove(item.path(), error);
  }
}

void DiskRomStorage::remove(const std::string& machine) {
  const fs::path directory = directoryOf(machine);
  std::error_code error;
  if (!fs::exists(directory, error)) return;
  fs::remove_all(directory, error);
  if (error) accessFailed("无法删除 ROM 目录", directory);
}

}  // namespace ember::infra::machines
