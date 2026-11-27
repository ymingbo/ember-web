/*
 * ------------------------------------------------------------------------
 * 名称：screenshot-service.hpp
 * 说明：output 契约：画面截图服务：截取最近一帧，编码成 PNG 或原始像素，返回数据或保存到调试输出目录。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * 备注：错误码以模块名 output 开头，取值与 schema/output.schema.json 登记的一致；保存文件只接受不含目录的文件名。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ember::contracts::output {

/** 调试输出目录的设置项标识（登记在“调试服务”分组，由 debug 模块登记，截图服务读取）。 */
inline constexpr const char* SETTING_DEBUG_OUTPUT_DIRECTORY = "debug.outputDirectory";

/** 还没有画面（没有开机，或还没有完成过一帧）。 */
inline constexpr std::string_view ERROR_NO_PICTURE = "output.noPicture";
/** 文件名不合格。 */
inline constexpr std::string_view ERROR_INVALID_FILE_NAME = "output.invalidFileName";
/** 无法创建目录或写入文件。 */
inline constexpr std::string_view ERROR_FILE_FAILED = "output.fileFailed";

/** 截图的结果。 */
struct ScreenshotResult {
  /** 格式：png 或 rgba。 */
  std::string format;
  std::uint32_t width{0};
  std::uint32_t height{0};
  /** 图像数据的字节数（保存到文件时也有）。 */
  std::size_t byteLength{0};
  /** 图像数据；保存到文件时为空。 */
  std::vector<std::uint8_t> data;
  /** 保存后的完整路径；没有保存时为空。 */
  std::string file;
};

/** 截图服务；可在任意线程调用。 */
class ScreenshotService {
 public:
  virtual ~ScreenshotService() = default;

  /**
   * 截取最近一帧。
   * @param format png 或 rgba。
   * @param fileName 给出时保存到调试输出目录下，不返回数据；只能是不含目录的文件名。
   * @throws shared::CoreError output.noPicture、output.invalidFileName、output.fileFailed。
   */
  virtual ScreenshotResult capture(const std::string& format, const std::optional<std::string>& fileName) = 0;
};

}  // namespace ember::contracts::output
