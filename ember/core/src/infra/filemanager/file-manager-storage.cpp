/*
 * ------------------------------------------------------------------------
 * 名称：file-manager-storage.cpp
 * 说明：目录里文件存取的磁盘实现。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-25
 * 备注：无。
 * ------------------------------------------------------------------------
 */

#include "infra/filemanager/file-manager-storage.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>

#include "contracts/filemanager/filemanager-error.hpp"
#include "shared/errors/core-error.hpp"

namespace ember::infra::filemanager {

namespace api = contracts::filemanager;
namespace fs = std::filesystem;

namespace {

/** 读取拖入内容的上限：层数、个数、总字节数。 */
constexpr int MAX_HOST_DEPTH = 16;
constexpr std::size_t MAX_HOST_ITEMS = 5000;
constexpr std::size_t MAX_HOST_BYTES = 64U * 1024U * 1024U;
/** 新建镜像文件时重名后缀的最大序号。 */
constexpr int MAX_NAME_SUFFIX = 9999;

[[noreturn]] void fail(std::string_view code, const std::string& message) { throw shared::CoreError(code, message); }

fs::path toPath(const std::string& text) { return fs::path(std::u8string(text.begin(), text.end())); }

std::string toUtf8(const fs::path& path) {
  const auto text = path.u8string();
  return std::string(text.begin(), text.end());
}

std::string lowered(std::string text) {
  for (char& c : text) {
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  }
  return text;
}

/** 文件的种类（按扩展名，不分大小写）；不在管理之内的为空。 */
std::optional<api::FileKind> kindOf(const fs::path& path) {
  std::string extension = lowered(toUtf8(path.extension()));
  if (!extension.empty()) extension.erase(extension.begin());
  return api::kindOfExtension(extension);
}

/** 相对路径的写法：不能是空的、绝对的、带盘符或反斜杠，每一级不能是空、“.”、“..”，也不能含 Windows 文件名里不允许的字符与控制字符。 */
void requireSafeRelative(const std::string& relative) {
  const std::string message = "路径不合格：" + relative;
  if (relative.empty() || relative.front() == '/' || relative.find('\\') != std::string::npos || relative.find(':') != std::string::npos) fail(api::ERROR_INVALID_PATH, message);
  std::size_t start = 0;
  while (true) {
    const std::size_t slash = relative.find('/', start);
    const std::string part = relative.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
    if (part.empty() || part == "." || part == ".." || part.find_first_of("<>\"|?*") != std::string::npos) fail(api::ERROR_INVALID_PATH, message);
    if (std::any_of(part.begin(), part.end(), [](char c) { return static_cast<unsigned char>(c) < 0x20; })) fail(api::ERROR_INVALID_PATH, message);
    if (slash == std::string::npos) break;
    start = slash + 1;
  }
}

/** path 规范化之后是否在 root 里面（符号链接指到外面也不算）。 */
bool isInside(const fs::path& root, const fs::path& path, bool allowSame) {
  std::error_code error;
  const fs::path canonicalRoot = fs::weakly_canonical(root, error);
  if (error) return false;
  const fs::path canonicalPath = fs::weakly_canonical(path, error);
  if (error) return false;
  const auto mismatch = std::mismatch(canonicalRoot.begin(), canonicalRoot.end(), canonicalPath.begin(), canonicalPath.end());
  if (mismatch.first != canonicalRoot.end()) return false;
  return allowSame || canonicalPath != canonicalRoot;
}

/** 目录下相对路径指向的软盘镜像文件：路径要合格、扩展名要是软盘镜像、规范化后要在目录里。 */
fs::path resolveImage(const std::string& root, const std::string& relative) {
  requireSafeRelative(relative);
  const fs::path rootPath = toPath(root);
  const fs::path full = rootPath / toPath(relative);
  if (kindOf(full) != api::FileKind::Floppy || !isInside(rootPath, full, false)) fail(api::ERROR_INVALID_PATH, "不是目录里的镜像文件：" + relative);
  return full;
}

/** 存在的普通文件。 */
fs::path resolveExistingImage(const std::string& root, const std::string& relative) {
  const fs::path full = resolveImage(root, relative);
  std::error_code error;
  if (!fs::is_regular_file(full, error) || error) fail(api::ERROR_IMAGE_NOT_FOUND, "找不到镜像文件：" + relative);
  return full;
}

/** 目录下存在的普通文件（不分扩展名）：路径要合格、规范化后要在目录里。 */
fs::path resolveExistingFile(const std::string& root, const std::string& relative) {
  requireSafeRelative(relative);
  const fs::path rootPath = toPath(root);
  const fs::path full = rootPath / toPath(relative);
  if (!isInside(rootPath, full, false)) fail(api::ERROR_INVALID_PATH, "不是目录里的文件：" + relative);
  std::error_code error;
  if (!fs::is_regular_file(full, error) || error) fail(api::ERROR_IMAGE_NOT_FOUND, "找不到文件：" + relative);
  return full;
}

/** 读取拖入内容时的累计。 */
struct HostBudget {
  std::size_t items{0};
  std::size_t bytes{0};
};

api::HostItem readHost(const fs::path& path, int depth, HostBudget& budget) {
  const std::string name = toUtf8(path.filename());
  if (depth > MAX_HOST_DEPTH || ++budget.items > MAX_HOST_ITEMS) fail(api::ERROR_INVALID_PATH, "拖入的内容太多（最多 5000 个文件、16 层文件夹）：" + name);
  std::error_code error;
  api::HostItem item;
  item.name = name;
  if (fs::is_directory(path, error)) {
    item.directory = true;
    fs::directory_iterator iterator(path, error);
    if (error) fail(api::ERROR_INVALID_PATH, "无法读取文件夹：" + name);
    for (; iterator != fs::directory_iterator(); iterator.increment(error)) {
      if (error) fail(api::ERROR_INVALID_PATH, "无法读取文件夹：" + name);
      std::error_code statusError;
      if (iterator->is_symlink(statusError) || statusError) continue;
      item.children.push_back(readHost(iterator->path(), depth + 1, budget));
    }
    std::sort(item.children.begin(), item.children.end(), [](const api::HostItem& a, const api::HostItem& b) {
      if (a.directory != b.directory) return a.directory;
      return lowered(a.name) < lowered(b.name);
    });
    return item;
  }
  if (!fs::is_regular_file(path, error)) fail(api::ERROR_INVALID_PATH, "找不到拖入的文件，或它不是普通文件：" + name);
  const std::uintmax_t size = fs::file_size(path, error);
  if (error) fail(api::ERROR_INVALID_PATH, "无法读取文件：" + name);
  if (size + budget.bytes > MAX_HOST_BYTES) fail(api::ERROR_INVALID_PATH, "拖入的内容超过 64 MB：" + name);
  std::ifstream stream(path, std::ios::binary);
  item.data.resize(static_cast<std::size_t>(size));
  stream.read(reinterpret_cast<char*>(item.data.data()), static_cast<std::streamsize>(item.data.size()));
  if (!stream || stream.gcount() != static_cast<std::streamsize>(item.data.size())) fail(api::ERROR_INVALID_PATH, "无法读取文件：" + name);
  budget.bytes += item.data.size();
  return item;
}

/** 文件名里不允许的字符。 */
constexpr const char* kForbiddenNameChars = "<>:\"/\\|?*";
/** 重名时可以试的最大序号。 */
constexpr int kMaxCopySuffix = 9999;

/** Windows 保留的设备名（不含扩展名）。 */
bool isReservedName(const std::string& name) {
  const std::string base = lowered(name.substr(0, name.find('.')));
  if (base == "con" || base == "prn" || base == "aux" || base == "nul") return true;
  return base.size() == 4 && (base.rfind("com", 0) == 0 || base.rfind("lpt", 0) == 0) && base[3] >= '1' && base[3] <= '9';
}

/** 文件或文件夹的名称：不能为空、太长、含不允许的字符、以空格或点结尾、是设备名。 */
void requireValidName(const std::string& name) {
  if (name.empty() || name.size() > 255) fail(api::ERROR_INVALID_NAME, "名称不能为空，也不能超过 255 个字节");
  if (name.find_first_of(kForbiddenNameChars) != std::string::npos || std::any_of(name.begin(), name.end(), [](char c) { return static_cast<unsigned char>(c) < 0x20; })) {
    fail(api::ERROR_INVALID_NAME, "名称不能包含下面任何字符：\\ / : * ? \" < > |");
  }
  if (name.back() == ' ' || name.back() == '.' || name == "." || name == ".." || isReservedName(name)) fail(api::ERROR_INVALID_NAME, "这个名称不能用：" + name);
}

/** 目录里的一个文件或文件夹：路径合格、在目录里、不是符号链接、存在，文件不分扩展名。 */
fs::path resolveExistingItem(const std::string& root, const std::string& relative, bool& directory) {
  requireSafeRelative(relative);
  const fs::path rootPath = toPath(root);
  const fs::path full = rootPath / toPath(relative);
  if (!isInside(rootPath, full, false)) fail(api::ERROR_INVALID_PATH, "不是目录里的文件或文件夹：" + relative);
  std::error_code error;
  const fs::file_status status = fs::symlink_status(full, error);
  if (error || !fs::exists(status)) fail(api::ERROR_ENTRY_NOT_FOUND, "找不到：" + relative);
  if (fs::is_symlink(status)) fail(api::ERROR_INVALID_PATH, "不支持符号链接：" + relative);
  directory = fs::is_directory(status);
  if (!directory && !fs::is_regular_file(status)) fail(api::ERROR_INVALID_PATH, "不是普通文件：" + relative);
  return full;
}

/** 目录里的一个文件夹，空串是目录本身。 */
fs::path resolveDirectory(const std::string& root, const std::string& relative) {
  const fs::path rootPath = toPath(root);
  if (relative.empty()) return rootPath;
  requireSafeRelative(relative);
  const fs::path full = rootPath / toPath(relative);
  std::error_code error;
  if (!isInside(rootPath, full, true) || !fs::is_directory(full, error) || error) fail(api::ERROR_INVALID_PATH, "不是目录里的文件夹：" + relative);
  return full;
}

std::string joinRelative(const std::string& directory, const std::string& name) { return directory.empty() ? name : directory + "/" + name; }

/** 父文件夹的相对路径（空串是目录本身）。 */
std::string parentOf(const std::string& relative) {
  const std::size_t slash = relative.find_last_of('/');
  return slash == std::string::npos ? std::string() : relative.substr(0, slash);
}

/** 复制时重名的新名称：“名 - 副本.扩展名”“名 - 副本 (2).扩展名”… */
std::string uniqueCopyName(const fs::path& directory, const std::string& name, bool file) {
  const fs::path original = toPath(name);
  const std::string stem = file ? toUtf8(original.stem()) : name;
  const std::string extension = file ? toUtf8(original.extension()) : std::string();
  std::error_code error;
  for (int number = 1; number <= kMaxCopySuffix; ++number) {
    const std::string candidate = stem + " - 副本" + (number == 1 ? std::string() : " (" + std::to_string(number) + ")") + extension;
    if (!fs::exists(directory / toPath(candidate), error)) return candidate;
  }
  fail(api::ERROR_SAVE_FAILED, "同名的副本太多：" + name);
}

}  // namespace

FileManagerStorageOnDisk::FileManagerStorageOnDisk(std::shared_ptr<platform::TrashBin> trash) : trash_(std::move(trash)) {}

bool FileManagerStorageOnDisk::isDirectory(const std::string& path) const {
  std::error_code error;
  return fs::is_directory(toPath(path), error) && !error;
}

std::vector<api::StoredImageInfo> FileManagerStorageOnDisk::list(const std::string& root, const std::string& relativeDirectory, bool& available) const {
  available = isDirectory(root);
  std::vector<api::StoredImageInfo> result;
  if (!available) return result;
  const fs::path directory = resolveDirectory(root, relativeDirectory);
  std::error_code error;
  fs::directory_iterator iterator(directory, fs::directory_options::skip_permission_denied, error);
  if (error) return result;
  for (; iterator != fs::directory_iterator(); iterator.increment(error)) {
    if (error) break;
    const fs::directory_entry& entry = *iterator;
    std::error_code statusError;
    const std::string relative = joinRelative(relativeDirectory, toUtf8(entry.path().filename()));
    // 符号链接与联接点不跟随（它们可能指到目录之外），但要列出来，免得文件夹看上去是空的，按不支持的文件显示
    const bool link = entry.is_symlink(statusError) || statusError;
    if (!link && entry.is_directory(statusError)) {
      result.push_back(api::StoredImageInfo{relative, true, 0, 0});
      continue;
    }
    std::error_code sizeError;
    std::error_code timeError;
    const std::uintmax_t size = link ? 0 : entry.file_size(sizeError);
    const auto written = link ? fs::file_time_type{} : entry.last_write_time(timeError);
    result.push_back(api::StoredImageInfo{relative, false, sizeError || link ? 0 : size, timeError || link ? 0 : static_cast<std::int64_t>(written.time_since_epoch().count())});
  }
  return result;
}

std::optional<api::StoredImageInfo> FileManagerStorageOnDisk::stat(const std::string& root, const std::string& relative) const {
  try {
    requireSafeRelative(relative);
  } catch (const shared::CoreError&) {
    return std::nullopt;
  }
  const fs::path full = toPath(root) / toPath(relative);
  std::error_code error;
  if (!fs::is_regular_file(full, error) || error) return std::nullopt;
  const std::uintmax_t size = fs::file_size(full, error);
  if (error) return std::nullopt;
  const auto written = fs::last_write_time(full, error);
  if (error) return std::nullopt;
  return api::StoredImageInfo{relative, false, size, static_cast<std::int64_t>(written.time_since_epoch().count())};
}

std::vector<std::uint8_t> FileManagerStorageOnDisk::read(const std::string& root, const std::string& relative) const {
  const fs::path path = resolveExistingImage(root, relative);
  std::error_code error;
  const std::uintmax_t size = fs::file_size(path, error);
  if (error) fail(api::ERROR_IMAGE_UNREADABLE, "无法读取镜像文件：" + relative);
  if (size == 0) fail(api::ERROR_IMAGE_UNREADABLE, "镜像文件是空的：" + relative);
  if (size > api::MAX_IMAGE_SIZE) fail(api::ERROR_IMAGE_UNREADABLE, "镜像文件超过 16 MB，不是软盘镜像：" + relative);
  std::ifstream stream(path, std::ios::binary);
  if (!stream) fail(api::ERROR_IMAGE_UNREADABLE, "无法打开镜像文件：" + relative);
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
  stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  if (stream.gcount() != static_cast<std::streamsize>(bytes.size())) fail(api::ERROR_IMAGE_UNREADABLE, "读取镜像文件时中断：" + relative);
  return bytes;
}

void FileManagerStorageOnDisk::writeAt(const std::string& root, const std::string& relative, std::size_t offset, std::span<const std::uint8_t> bytes) {
  const fs::path path = resolveExistingImage(root, relative);
  std::error_code error;
  const std::uintmax_t size = fs::file_size(path, error);
  // 文件被别的程序改短或换掉时不在这里重建，直接报错让用户知道
  if (error || size < offset + bytes.size()) fail(api::ERROR_SAVE_FAILED, "镜像文件被改动过，无法写入：" + relative);
  std::fstream stream(path, std::ios::binary | std::ios::in | std::ios::out);
  stream.seekp(static_cast<std::streamoff>(offset));
  stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  stream.flush();
  if (!stream) fail(api::ERROR_SAVE_FAILED, "无法写入镜像文件：" + relative);
}

std::string FileManagerStorageOnDisk::create(const std::string& root, const std::string& relativeDirectory, const std::string& baseName, const std::string& extension, const std::vector<std::uint8_t>& bytes) {
  const fs::path rootPath = toPath(root);
  fs::path directory = rootPath;
  if (!relativeDirectory.empty()) {
    requireSafeRelative(relativeDirectory);
    directory = rootPath / toPath(relativeDirectory);
  }
  std::error_code error;
  if (!isInside(rootPath, directory, true) || !fs::is_directory(directory, error) || error) fail(api::ERROR_INVALID_PATH, "不是目录里的文件夹：" + relativeDirectory);
  std::string fileName;
  fs::path target;
  for (int number = 1; number <= MAX_NAME_SUFFIX; ++number) {
    fileName = baseName + (number == 1 ? std::string() : " (" + std::to_string(number) + ")") + "." + extension;
    target = directory / toPath(fileName);
    if (!fs::exists(target, error)) break;
    if (number == MAX_NAME_SUFFIX) fail(api::ERROR_SAVE_FAILED, "同名的镜像文件太多");
  }
  fs::path temporary = target;
  temporary += ".tmp";
  {
    std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
    stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    stream.flush();
    if (!stream) {
      stream.close();
      fs::remove(temporary, error);
      fail(api::ERROR_SAVE_FAILED, "无法创建镜像文件：" + fileName);
    }
  }
  fs::rename(temporary, target, error);
  if (error) {
    fs::remove(temporary, error);
    fail(api::ERROR_SAVE_FAILED, "无法创建镜像文件：" + fileName);
  }
  return relativeDirectory.empty() ? fileName : relativeDirectory + "/" + fileName;
}

std::string FileManagerStorageOnDisk::absolutePath(const std::string& root, const std::string& relative) const {
  std::error_code error;
  const fs::path absolute = fs::absolute(resolveExistingFile(root, relative), error);
  if (error) fail(api::ERROR_IMAGE_NOT_FOUND, "找不到文件：" + relative);
  return toUtf8(absolute);
}

bool FileManagerStorageOnDisk::samePath(const std::string& a, const std::string& b) const {
  std::error_code error;
  const fs::path left = toPath(a);
  const fs::path right = toPath(b);
  const bool equivalent = fs::equivalent(left, right, error);
  if (!error) return equivalent;
  // 其中一个已经不存在：只能按写法比较
  return lowered(toUtf8(left.lexically_normal())) == lowered(toUtf8(right.lexically_normal()));
}

api::HostItem FileManagerStorageOnDisk::readHostItem(const std::string& path) const {
  HostBudget budget;
  return readHost(toPath(path), 0, budget);
}

bool FileManagerStorageOnDisk::covers(const std::string& root, const std::string& relative, const std::string& absolute) const {
  bool directory = false;
  try {
    return isInside(resolveExistingItem(root, relative, directory), toPath(absolute), true);
  } catch (const shared::CoreError&) {
    return false;
  }
}

std::string FileManagerStorageOnDisk::renameItem(const std::string& root, const std::string& relative, const std::string& newName) {
  bool directory = false;
  const fs::path source = resolveExistingItem(root, relative, directory);
  requireValidName(newName);
  const std::string parent = parentOf(relative);
  const std::string current = toUtf8(source.filename());
  if (current == newName) return relative;
  const fs::path target = source.parent_path() / toPath(newName);
  std::error_code error;
  // 只改大小写时目标“已存在”的就是它自己
  if (fs::exists(target, error) && !fs::equivalent(source, target, error)) fail(api::ERROR_EXISTS, "已经有同名的文件或文件夹：" + newName);
  fs::rename(source, target, error);
  if (error) fail(api::ERROR_SAVE_FAILED, "无法重命名：" + current);
  return joinRelative(parent, newName);
}

std::vector<std::string> FileManagerStorageOnDisk::moveItems(const std::string& root, const std::vector<std::string>& relatives, const std::string& targetDirectory) {
  const fs::path target = resolveDirectory(root, targetDirectory);
  std::vector<std::pair<fs::path, std::string>> plan;
  std::vector<std::string> results;
  std::error_code error;
  for (const std::string& relative : relatives) {
    bool directory = false;
    const fs::path source = resolveExistingItem(root, relative, directory);
    const std::string name = toUtf8(source.filename());
    results.push_back(joinRelative(targetDirectory, name));
    if (parentOf(relative) == targetDirectory) continue;
    if (directory && isInside(source, target, true)) fail(api::ERROR_MOVE_INTO_SELF, "不能把文件夹移到它自己里面：" + name);
    if (fs::exists(target / toPath(name), error)) fail(api::ERROR_EXISTS, "目标文件夹里已经有同名的文件或文件夹：" + name);
    plan.emplace_back(source, name);
  }
  for (const auto& [source, name] : plan) {
    fs::rename(source, target / toPath(name), error);
    if (error) fail(api::ERROR_SAVE_FAILED, "无法移动：" + name);
  }
  return results;
}

std::vector<std::string> FileManagerStorageOnDisk::copyItems(const std::string& root, const std::vector<std::string>& relatives, const std::string& targetDirectory) {
  const fs::path target = resolveDirectory(root, targetDirectory);
  std::vector<std::string> results;
  std::error_code error;
  for (const std::string& relative : relatives) {
    bool directory = false;
    const fs::path source = resolveExistingItem(root, relative, directory);
    const std::string name = toUtf8(source.filename());
    if (directory && isInside(source, target, true)) fail(api::ERROR_MOVE_INTO_SELF, "不能把文件夹复制到它自己里面：" + name);
    const std::string copyName = fs::exists(target / toPath(name), error) ? uniqueCopyName(target, name, !directory) : name;
    const fs::path destination = target / toPath(copyName);
    if (directory) fs::copy(source, destination, fs::copy_options::recursive | fs::copy_options::skip_symlinks, error);
    else fs::copy_file(source, destination, fs::copy_options::none, error);
    if (error) fail(api::ERROR_SAVE_FAILED, "无法复制：" + name);
    results.push_back(joinRelative(targetDirectory, copyName));
  }
  return results;
}

void FileManagerStorageOnDisk::removeItems(const std::string& root, const std::vector<std::string>& relatives) {
  std::vector<std::string> paths;
  for (const std::string& relative : relatives) {
    bool directory = false;
    paths.push_back(toUtf8(resolveExistingItem(root, relative, directory)));
  }
  if (trash_ != nullptr) {
    if (!trash_->moveToTrash(paths)) fail(api::ERROR_SAVE_FAILED, "无法移进回收站");
    return;
  }
  std::error_code error;
  for (const std::string& path : paths) {
    fs::remove_all(toPath(path), error);
    if (error) fail(api::ERROR_SAVE_FAILED, "无法删除：" + path);
  }
}

std::string FileManagerStorageOnDisk::makeFolder(const std::string& root, const std::string& relativeDirectory, const std::string& baseName) {
  const fs::path directory = resolveDirectory(root, relativeDirectory);
  requireValidName(baseName);
  std::error_code error;
  for (int number = 1; number <= kMaxCopySuffix; ++number) {
    const std::string name = baseName + (number == 1 ? std::string() : " (" + std::to_string(number) + ")");
    const fs::path path = directory / toPath(name);
    if (fs::exists(path, error)) continue;
    if (!fs::create_directory(path, error) || error) fail(api::ERROR_SAVE_FAILED, "无法新建文件夹：" + name);
    return joinRelative(relativeDirectory, name);
  }
  fail(api::ERROR_SAVE_FAILED, "同名的文件夹太多：" + baseName);
}

}  // namespace ember::infra::filemanager
