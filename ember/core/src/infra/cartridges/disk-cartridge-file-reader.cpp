/*
 * ------------------------------------------------------------------------
 * 名称：disk-cartridge-file-reader.cpp
 * 说明：卡带文件读取的实现：检查文件存在且是普通文件、大小不超过上限，再整个读入。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * ------------------------------------------------------------------------
 */

#include "infra/cartridges/disk-cartridge-file-reader.hpp"

#include <filesystem>
#include <fstream>

#include "contracts/cartridges/cartridge-error.hpp"
#include "shared/errors/core-error.hpp"

namespace ember::infra::cartridges {

namespace api = contracts::cartridges;
namespace fs = std::filesystem;

std::vector<std::uint8_t> DiskCartridgeFileReader::read(const std::string& path) const {
  const fs::path file(std::u8string(path.begin(), path.end()));
  std::error_code error;
  const auto status = fs::status(file, error);
  if (error || !fs::exists(status)) throw shared::CoreError(api::ERROR_FILE_NOT_FOUND, "找不到文件：" + path);
  if (!fs::is_regular_file(status)) throw shared::CoreError(api::ERROR_FILE_UNREADABLE, "不是普通文件：" + path);
  const auto size = fs::file_size(file, error);
  if (error) throw shared::CoreError(api::ERROR_FILE_UNREADABLE, "无法读取文件：" + path);
  if (size > MAX_FILE_SIZE) throw shared::CoreError(api::ERROR_UNSUPPORTED_FORMAT, "文件超过 64 MB，不是 NES 卡带：" + path);

  std::ifstream stream(file, std::ios::binary);
  if (!stream) throw shared::CoreError(api::ERROR_FILE_UNREADABLE, "无法打开文件：" + path);
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
  stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  if (stream.gcount() != static_cast<std::streamsize>(bytes.size())) throw shared::CoreError(api::ERROR_FILE_UNREADABLE, "读取文件时中断：" + path);
  return bytes;
}

}  // namespace ember::infra::cartridges
