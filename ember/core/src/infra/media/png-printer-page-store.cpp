/*
 * ------------------------------------------------------------------------
 * 名称：png-printer-page-store.cpp
 * 说明：打印页保存的实现。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-15
 * ------------------------------------------------------------------------
 */

#include "infra/media/png-printer-page-store.hpp"

#include <miniz.h>

#include <filesystem>
#include <fstream>

#include "contracts/media/media-error.hpp"
#include "shared/errors/core-error.hpp"

namespace ember::infra::media {

namespace api = contracts::media;
namespace fs = std::filesystem;

namespace {

constexpr std::uint8_t PNG_SIGNATURE[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};

void appendBigEndian(std::vector<std::uint8_t>& out, std::uint32_t value) {
  out.push_back(static_cast<std::uint8_t>(value >> 24));
  out.push_back(static_cast<std::uint8_t>(value >> 16));
  out.push_back(static_cast<std::uint8_t>(value >> 8));
  out.push_back(static_cast<std::uint8_t>(value));
}

/** 追加一个 PNG 数据块：长度、类型、数据、校验。 */
void appendChunk(std::vector<std::uint8_t>& out, const char (&type)[5], const std::vector<std::uint8_t>& data) {
  appendBigEndian(out, static_cast<std::uint32_t>(data.size()));
  const auto typeStart = out.size();
  out.insert(out.end(), type, type + 4);
  out.insert(out.end(), data.begin(), data.end());
  const mz_ulong crc = mz_crc32(MZ_CRC32_INIT, out.data() + typeStart, out.size() - typeStart);
  appendBigEndian(out, static_cast<std::uint32_t>(crc));
}

fs::path toPath(const std::string& text) { return fs::path(std::u8string(text.begin(), text.end())); }

[[noreturn]] void saveFailed(const std::string& what) { throw shared::CoreError(api::ERROR_SAVE_FAILED, what); }

}  // namespace

std::vector<std::uint8_t> PngPrinterPageStore::encode(const contracts::hardware::PrintedPage& page) {
  const std::size_t rowBytes = (page.width + 7) / 8;
  // 每行前面加一个滤波类型字节（0：不滤波）；PNG 里 0 是黑、1 是白，所以取反。
  std::vector<std::uint8_t> raw;
  raw.reserve((rowBytes + 1) * page.height);
  for (unsigned row = 0; row < page.height; ++row) {
    raw.push_back(0);
    for (std::size_t column = 0; column < rowBytes; ++column) raw.push_back(static_cast<std::uint8_t>(~page.rows[row * rowBytes + column]));
  }
  mz_ulong compressedSize = mz_compressBound(static_cast<mz_ulong>(raw.size()));
  std::vector<std::uint8_t> compressed(compressedSize);
  if (mz_compress2(compressed.data(), &compressedSize, raw.data(), static_cast<mz_ulong>(raw.size()), 6) != MZ_OK) saveFailed("无法压缩打印页");
  compressed.resize(compressedSize);

  std::vector<std::uint8_t> header;
  appendBigEndian(header, page.width);
  appendBigEndian(header, page.height);
  header.push_back(1);  // 位深
  header.push_back(0);  // 灰度
  header.push_back(0);  // 压缩方法
  header.push_back(0);  // 滤波方法
  header.push_back(0);  // 不隔行

  std::vector<std::uint8_t> png(PNG_SIGNATURE, PNG_SIGNATURE + sizeof(PNG_SIGNATURE));
  appendChunk(png, "IHDR", header);
  appendChunk(png, "IDAT", compressed);
  appendChunk(png, "IEND", {});
  return png;
}

std::string PngPrinterPageStore::save(const std::string& directory, const std::string& baseName, const contracts::hardware::PrintedPage& page) {
  const fs::path folder = toPath(directory);
  std::error_code error;
  fs::create_directories(folder, error);
  if (error) saveFailed("无法创建保存目录：" + directory);

  // 同名文件已存在时在文件名后加 -2、-3……
  std::string name = baseName + ".png";
  for (unsigned serial = 2; fs::exists(folder / toPath(name), error) && serial < 10000; ++serial) name = baseName + "-" + std::to_string(serial) + ".png";

  const std::vector<std::uint8_t> bytes = encode(page);
  const fs::path target = folder / toPath(name);
  fs::path temporary = target;
  temporary += ".tmp";
  {
    std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
    stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    stream.flush();
    if (!stream) {
      stream.close();
      fs::remove(temporary, error);
      saveFailed("无法写入打印页：" + name);
    }
  }
  fs::rename(temporary, target, error);
  if (error) {
    fs::remove(temporary, error);
    saveFailed("无法保存打印页：" + name);
  }
  return name;
}

}  // namespace ember::infra::media
