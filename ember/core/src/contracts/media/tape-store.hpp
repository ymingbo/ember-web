/*
 * ------------------------------------------------------------------------
 * 名称：tape-store.hpp
 * 说明：磁带文件存取的契约：把声音文件打开成磁带的声音来源，为录音创建声音文件，在已有的磁带上从某个位置开始改写。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-15
 * 备注：解码与文件格式在实现里（infra/media）；路径只来自用户在系统窗口里的选择或调试服务的请求。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <memory>
#include <string>

#include "contracts/hardware/tape-deck.hpp"

namespace ember::contracts::media {

/** 磁带文件的存取。 */
class TapeStore {
 public:
  virtual ~TapeStore() = default;

  /**
   * 打开声音文件作为磁带；文件内容的解码在返回的来源被读取时才进行。
   * @param path 声音文件的路径。
   * @throws shared::CoreError media.fileNotFound、media.fileUnreadable、media.tapeUnsupported（没有声音或无法解码）。
   */
  virtual std::unique_ptr<hardware::TapeSource> open(const std::string& path) const = 0;

  /**
   * 创建（覆盖）录音文件：单声道 16 位的 WAV；来源的 finish 之后文件才完整。
   * @param path 录音文件的路径。
   * @throws shared::CoreError media.saveFailed。
   */
  virtual std::unique_ptr<hardware::TapeSink> create(const std::string& path) const = 0;

  /**
   * 在已有的磁带文件上从某个位置开始录音：返回的去向收到新录的内容，覆盖这个位置之后相同长的一段；位置之前与录音结束处之后的原有内容保留；
   * 来源的 finish 之后文件才被替换成完整的新内容，没有 finish 就销毁时原文件不动。录好的文件是 96 kHz、16 位、立体声的 WAV（新录的部分左右声道相同）。
   * 位置之前的内容在这个函数里复制，可能要一段时间，所以不要在模拟线程里调用。
   * @param path 磁带文件的路径，必须是 WAV 文件。
   * @param startSeconds 录音开始处在磁带上的位置（秒）。
   * @throws shared::CoreError media.fileNotFound、media.fileUnreadable、media.tapeUnsupported、media.tapeNotWritable（不是 WAV）、media.saveFailed。
   */
  virtual std::unique_ptr<hardware::TapeSink> overwrite(const std::string& path, double startSeconds) const = 0;
};

}  // namespace ember::contracts::media
