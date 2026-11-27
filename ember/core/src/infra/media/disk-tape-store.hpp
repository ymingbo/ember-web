/*
 * ------------------------------------------------------------------------
 * 名称：disk-tape-store.hpp
 * 说明：磁带文件存取的磁盘实现：用静态链接的 FFmpeg 库把声音文件解码成 96 kHz 立体声的采样流，录音写成 32 kHz 单声道的 WAV 文件；在已有的 WAV 磁带上从某个位置开始录音时，合成 96 kHz 立体声的 WAV 替换原文件。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-15
 * 备注：打开时先解封装读出时长与声道数（不解码）；解码在来源第一次被读取时才开始，边读边解，所以长磁带不占内存；
 *       来源的 rewind 关闭文件，下次读取重新打开；解码中途出错时当作磁带放完并写系统错误日志；FFmpeg 的头文件只在 .cpp 里使用。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <memory>
#include <string>

#include "contracts/media/tape-store.hpp"
#include "platform/logging/error-log.hpp"

namespace ember::infra::media {

/** 磁盘上的磁带文件存取。 */
class DiskTapeStore final : public contracts::media::TapeStore {
 public:
  /** @param errors 系统错误日志，解码中途出错时写入。 */
  explicit DiskTapeStore(std::shared_ptr<platform::ErrorLog> errors);

  [[nodiscard]] std::unique_ptr<contracts::hardware::TapeSource> open(const std::string& path) const override;
  [[nodiscard]] std::unique_ptr<contracts::hardware::TapeSink> create(const std::string& path) const override;
  [[nodiscard]] std::unique_ptr<contracts::hardware::TapeSink> overwrite(const std::string& path, double startSeconds) const override;

 private:
  std::shared_ptr<platform::ErrorLog> errors_;
};

}  // namespace ember::infra::media
