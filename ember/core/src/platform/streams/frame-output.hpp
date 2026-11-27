/*
 * ------------------------------------------------------------------------
 * 名称：frame-output.hpp
 * 说明：帧输出的抽象：模拟线程每完成一帧，把完成颜色转换的帧缓冲交给它；实现负责把画面送到界面。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * 备注：像素格式见 docs/02-硬件模块.md 第 6.1 节：每像素 4 字节，内存顺序 R、G、B、A。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <cstdint>
#include <span>

namespace ember::platform {

/** 一帧画面；像素只在 publish 调用期间有效。 */
struct FrameView {
  std::uint32_t width{0};
  std::uint32_t height{0};
  /** 帧号：自上电以来完成的帧数。 */
  std::uint64_t number{0};
  /** 按行从上到下存放的像素，共 width × height 个。 */
  std::span<const std::uint32_t> pixels;
};

/** 帧输出。 */
class FrameOutput {
 public:
  virtual ~FrameOutput() = default;

  /**
   * 发布一帧。在模拟线程调用，不能阻塞；实现必须在返回前复制走像素，处理不过来时丢弃这一帧。
   */
  virtual void publish(const FrameView& frame) noexcept = 0;
};

}  // namespace ember::platform
