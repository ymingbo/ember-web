/*
 * ------------------------------------------------------------------------
 * 名称：png-screenshot-service.cpp
 * 说明：截图服务的实现，见 png-screenshot-service.hpp。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * ------------------------------------------------------------------------
 */

#include "infra/output/png-screenshot-service.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <utility>

#include <miniz.h>

#include "shared/errors/core-error.hpp"

namespace ember::infra::output {

namespace api = contracts::output;
namespace fs = std::filesystem;

namespace {

constexpr std::size_t MAX_FILE_NAME_LENGTH = 100;

/** RGBA 转成 RGB（截图的画面不透明），PNG 小一些。 */
std::vector<std::uint8_t> toRgb(const std::vector<std::uint8_t>& rgba) {
  std::vector<std::uint8_t> rgb;
  rgb.reserve(rgba.size() / 4 * 3);
  for (std::size_t i = 0; i + 3 < rgba.size(); i += 4) {
    rgb.push_back(rgba[i]);
    rgb.push_back(rgba[i + 1]);
    rgb.push_back(rgba[i + 2]);
  }
  return rgb;
}

/** 编码成 PNG。 */
std::vector<std::uint8_t> encodePng(const std::vector<std::uint8_t>& rgba, std::uint32_t width, std::uint32_t height) {
  const auto rgb = toRgb(rgba);
  std::size_t length = 0;
  void* encoded = tdefl_write_image_to_png_file_in_memory_ex(rgb.data(), static_cast<int>(width), static_cast<int>(height), 3, &length, 6, MZ_FALSE);
  if (encoded == nullptr) throw shared::CoreError(api::ERROR_FILE_FAILED, "无法编码 PNG");
  const auto* bytes = static_cast<const std::uint8_t*>(encoded);
  std::vector<std::uint8_t> png(bytes, bytes + length);
  mz_free(encoded);
  return png;
}

bool endsWithIgnoringCase(const std::string& text, const std::string& suffix) {
  if (text.size() < suffix.size()) return false;
  return std::equal(suffix.rbegin(), suffix.rend(), text.rbegin(), [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); });
}

}  // namespace

bool isValidScreenshotFileName(const std::string& name) {
  if (name.empty() || name.size() > MAX_FILE_NAME_LENGTH) return false;
  if (name.front() == '.' || name.front() == ' ' || name.back() == '.' || name.back() == ' ') return false;
  if (name.find("..") != std::string::npos) return false;
  if (!std::ranges::all_of(name, [](unsigned char ch) { return std::isalnum(ch) != 0 || ch == '.' || ch == '_' || ch == '-' || ch == ' '; })) return false;
  // Windows 的设备名（带不带扩展名都不能用）。
  std::string stem = name.substr(0, name.find('.'));
  std::ranges::transform(stem, stem.begin(), [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
  static const char* const RESERVED[] = {"CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9", "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"};
  return std::ranges::none_of(RESERVED, [&stem](const char* reserved) { return stem == reserved; });
}

PngScreenshotService::PngScreenshotService(std::function<std::filesystem::path()> outputDirectory) : outputDirectory_(std::move(outputDirectory)) {}

void PngScreenshotService::bindRuntime(std::shared_ptr<contracts::runtime::RuntimeService> runtime) { runtime_ = std::move(runtime); }

api::ScreenshotResult PngScreenshotService::capture(const std::string& format, const std::optional<std::string>& fileName) {
  if (format != "png" && format != "rgba") throw shared::SchemaError("format 只能是 png 或 rgba");
  std::string name;
  if (fileName.has_value()) {
    name = *fileName;
    if (!isValidScreenshotFileName(name)) throw shared::CoreError(api::ERROR_INVALID_FILE_NAME, "文件名只能含字母、数字、点、下划线、短横线与空格，不能含目录，不能以点或空格开头与结尾");
    if (format == "png" && !endsWithIgnoringCase(name, ".png")) name += ".png";
  }

  auto image = runtime_ == nullptr ? contracts::runtime::FrameImage{} : runtime_->captureFrame();
  if (image.width == 0 || image.height == 0) throw shared::CoreError(api::ERROR_NO_PICTURE, "还没有画面：没有开机，或还没有完成过一帧");

  api::ScreenshotResult result;
  result.format = format;
  result.width = image.width;
  result.height = image.height;
  result.data = format == "png" ? encodePng(image.rgba, image.width, image.height) : std::move(image.rgba);
  result.byteLength = result.data.size();
  if (!fileName.has_value()) return result;

  try {
    const fs::path directory = outputDirectory_();
    fs::create_directories(directory);
    const fs::path target = directory / fs::path(std::u8string(name.begin(), name.end()));
    fs::path temporary = target;
    temporary += ".tmp";
    {
      std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
      stream.write(reinterpret_cast<const char*>(result.data.data()), static_cast<std::streamsize>(result.data.size()));
      stream.flush();
      if (!stream) throw std::runtime_error("无法写入文件");
    }
    fs::rename(temporary, target);
    const auto text = fs::absolute(target).u8string();
    result.file.assign(text.begin(), text.end());
  } catch (const std::exception& error) {
    throw shared::CoreError(api::ERROR_FILE_FAILED, std::string("无法保存截图：") + error.what());
  }
  result.data.clear();
  return result;
}

}  // namespace ember::infra::output
