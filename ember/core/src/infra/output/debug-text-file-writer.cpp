/*
 * ------------------------------------------------------------------------
 * 名称：debug-text-file-writer.cpp
 * 说明：调试输出文件写入的实现，见 debug-text-file-writer.hpp。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * ------------------------------------------------------------------------
 */

#include "infra/output/debug-text-file-writer.hpp"

#include <fstream>
#include <stdexcept>
#include <utility>

#include "infra/output/png-screenshot-service.hpp"
#include "shared/errors/core-error.hpp"

namespace ember::infra::output {

namespace api = contracts::output;
namespace fs = std::filesystem;

DebugTextFileWriter::DebugTextFileWriter(std::function<fs::path()> outputDirectory) : outputDirectory_(std::move(outputDirectory)) {}

void DebugTextFileWriter::validateName(const std::string& name) const {
  if (!isValidScreenshotFileName(name)) throw shared::CoreError(api::ERROR_INVALID_FILE_NAME, "文件名只能含字母、数字、点、下划线、短横线与空格，不能含目录，不能以点或空格开头与结尾");
}

std::string DebugTextFileWriter::writeLines(const std::string& fileName, const std::vector<std::string>& lines) {
  return writeFile(fileName, [&lines](std::ostream& stream) {
    for (const std::string& line : lines) stream << line << '\n';
  });
}

std::string DebugTextFileWriter::writeBytes(const std::string& fileName, std::span<const std::uint8_t> bytes) {
  return writeFile(fileName, [bytes](std::ostream& stream) { stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())); });
}

std::string DebugTextFileWriter::writeFile(const std::string& fileName, const std::function<void(std::ostream&)>& fill) {
  validateName(fileName);
  try {
    const fs::path directory = outputDirectory_();
    fs::create_directories(directory);
    const fs::path target = directory / fs::path(std::u8string(fileName.begin(), fileName.end()));
    fs::path temporary = target;
    temporary += ".tmp";
    {
      std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
      fill(stream);
      stream.flush();
      if (!stream) throw std::runtime_error("无法写入文件");
    }
    fs::rename(temporary, target);
    const auto text = fs::absolute(target).u8string();
    return std::string(text.begin(), text.end());
  } catch (const std::exception& error) {
    throw shared::CoreError(api::ERROR_FILE_FAILED, std::string("无法保存文件：") + error.what());
  }
}

}  // namespace ember::infra::output
