/*
 * ------------------------------------------------------------------------
 * 名称：debug-file-writer.hpp
 * 说明：output 契约：调试输出文件的写入：把文本或字节写成调试输出目录下的文件，供检查器保存跟踪、调用记录与代码数据标记。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * 备注：只接受不含目录的文件名，规则与截图保存相同；错误码见 screenshot-service.hpp（output.invalidFileName、output.fileFailed）。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace ember::contracts::output {

/** 调试输出文件的写入；可在任意线程调用。 */
class DebugFileWriter {
 public:
  virtual ~DebugFileWriter() = default;

  /**
   * 检查文件名：只含字母、数字、点、下划线、短横线与空格，不含目录，不以点或空格开头与结尾。
   * @throws shared::CoreError output.invalidFileName。
   */
  virtual void validateName(const std::string& name) const = 0;

  /**
   * 把文本行写成调试输出目录下的文件，每行以换行结尾；同名文件被替换。
   * @return 文件的完整路径。
   * @throws shared::CoreError output.invalidFileName、output.fileFailed。
   */
  virtual std::string writeLines(const std::string& fileName, const std::vector<std::string>& lines) = 0;

  /**
   * 把字节写成调试输出目录下的文件；同名文件被替换。
   * @return 文件的完整路径。
   * @throws shared::CoreError output.invalidFileName、output.fileFailed。
   */
  virtual std::string writeBytes(const std::string& fileName, std::span<const std::uint8_t> bytes) = 0;
};

}  // namespace ember::contracts::output
