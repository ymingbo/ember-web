/*
 * ------------------------------------------------------------------------
 * 名称：disk-floppy-image-store.cpp
 * 说明：盘片镜像文件存取的磁盘实现。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-15
 * ------------------------------------------------------------------------
 */

#include "infra/media/disk-floppy-image-store.hpp"

#include <filesystem>
#include <fstream>

#include "contracts/media/media-error.hpp"
#include "shared/errors/core-error.hpp"

namespace ember::infra::media {

namespace api = contracts::media;
namespace fs = std::filesystem;

namespace {

fs::path toPath(const std::string& text) { return fs::path(std::u8string(text.begin(), text.end())); }

}  // namespace

std::vector<std::uint8_t> DiskFloppyImageStore::read(const std::string& path) const {
  const fs::path file = toPath(path);
  std::error_code error;
  const auto status = fs::status(file, error);
  if (error || !fs::exists(status)) throw shared::CoreError(api::ERROR_FILE_NOT_FOUND, "找不到文件：" + path);
  if (!fs::is_regular_file(status)) throw shared::CoreError(api::ERROR_FILE_UNREADABLE, "不是普通文件：" + path);
  const auto size = fs::file_size(file, error);
  if (error) throw shared::CoreError(api::ERROR_FILE_UNREADABLE, "无法读取文件：" + path);
  if (size == 0) throw shared::CoreError(api::ERROR_IMAGE_INVALID, "镜像文件是空的：" + path);
  if (size > MAX_FILE_SIZE) throw shared::CoreError(api::ERROR_IMAGE_INVALID, "镜像文件超过 16 MB，不是软盘镜像：" + path);

  std::ifstream stream(file, std::ios::binary);
  if (!stream) throw shared::CoreError(api::ERROR_FILE_UNREADABLE, "无法打开文件：" + path);
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
  stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  if (stream.gcount() != static_cast<std::streamsize>(bytes.size())) throw shared::CoreError(api::ERROR_FILE_UNREADABLE, "读取文件时中断：" + path);
  return bytes;
}

void DiskFloppyImageStore::write(const std::string& path, const std::vector<std::uint8_t>& bytes) {
  const fs::path target = toPath(path);
  fs::path temporary = target;
  temporary += ".tmp";
  std::error_code error;
  {
    std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
    stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    stream.flush();
    if (!stream) {
      stream.close();
      fs::remove(temporary, error);
      throw shared::CoreError(api::ERROR_SAVE_FAILED, "无法写入镜像文件：" + path);
    }
  }
  fs::rename(temporary, target, error);
  if (error) {
    fs::remove(temporary, error);
    throw shared::CoreError(api::ERROR_SAVE_FAILED, "无法保存镜像文件：" + path);
  }
}

void DiskFloppyImageStore::writeAt(const std::string& path, std::size_t offset, std::span<const std::uint8_t> bytes) {
  const fs::path target = toPath(path);
  std::error_code error;
  const auto size = fs::file_size(target, error);
  // 文件被外部改短或删除时不在这里重建，直接报错让用户知道。
  if (error || size < offset + bytes.size()) throw shared::CoreError(api::ERROR_SAVE_FAILED, "镜像文件不存在或大小不对：" + path);
  std::fstream stream(target, std::ios::binary | std::ios::in | std::ios::out);
  stream.seekp(static_cast<std::streamoff>(offset));
  stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  stream.flush();
  if (!stream) throw shared::CoreError(api::ERROR_SAVE_FAILED, "无法写入镜像文件：" + path);
}

}  // namespace ember::infra::media
