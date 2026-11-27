/*
 * ------------------------------------------------------------------------
 * 名称：png-screenshot-service.hpp
 * 说明：截图服务的实现：从运行服务取最近一帧，编码成 PNG（miniz）或原始 RGBA，按需保存到调试输出目录。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * 备注：输出目录每次保存时现取（首选项里改了立即生效）；文件名只能是不含目录的名字，先写临时文件再改名，不覆盖不相干的文件之外的位置。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "contracts/output/screenshot-service.hpp"
#include "contracts/runtime/runtime-service.hpp"

namespace ember::infra::output {

/** 文件名是否合格：1 到 100 个字符，只含字母、数字、点、下划线、短横线与空格，不以点或空格开头与结尾，不含目录。 */
[[nodiscard]] bool isValidScreenshotFileName(const std::string& name);

/** PNG 截图服务。 */
class PngScreenshotService final : public contracts::output::ScreenshotService {
 public:
  /**
   * @param outputDirectory 取调试输出目录的函数。
   */
  explicit PngScreenshotService(std::function<std::filesystem::path()> outputDirectory);

  /** 绑定运行服务（提供最近一帧）；在全部模块登记完之后、处理请求之前调用一次，绑定之前没有画面。 */
  void bindRuntime(std::shared_ptr<contracts::runtime::RuntimeService> runtime);

  contracts::output::ScreenshotResult capture(const std::string& format, const std::optional<std::string>& fileName) override;

 private:
  std::shared_ptr<contracts::runtime::RuntimeService> runtime_;
  std::function<std::filesystem::path()> outputDirectory_;
};

}  // namespace ember::infra::output
