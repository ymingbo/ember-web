/*
 * ------------------------------------------------------------------------
 * 名称：ppu.hpp
 * 说明：硬件契约：PPU。复位、按时间追赶、输出帧缓冲；寄存器读写经 BusDevice 挂在 CPU 总线上。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * 备注：PPU 按点时钟（dot）精度运行，但不逐周期被调用：在 CPU 访问它之前、自己的事件到期时追赶到当前时间。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <cstdint>
#include <span>

#include "contracts/hardware/bus-device.hpp"
#include "contracts/hardware/clock.hpp"

namespace ember::contracts::hardware {

/**
 * 一帧画面：按行从上到下存放，每像素 4 个字节，内存中的字节顺序依次为 R、G、B、A（A 恒为 255）。
 * 以 32 位整数读取时，小端机器上的值为 0xAABBGGRR；核心只支持小端机器。画面可以不经转换直接上传为 RGBA8 纹理。
 */
struct FrameBuffer {
  std::uint32_t width{0};
  std::uint32_t height{0};
  std::span<const std::uint32_t> pixels;
};

/** PPU 的扫描位置：下一个要处理的点；调试用，不追赶时间，不产生副作用。 */
struct VideoPosition {
  /** 帧序号：从 0 起，每帧从第 0 行开始（与 frameCount 不同，frameCount 在画面完成的那一行就加 1）。 */
  std::uint64_t frame{0};
  /** 扫描线。 */
  std::int32_t scanline{0};
  /** 点；按行处理的 PPU 恒为 0。 */
  std::int32_t dot{0};
};

/** PPU。 */
class Ppu : public BusDevice {
 public:
  /**
   * 复位。
   * @param power 为 true 表示上电复位，为 false 表示软复位。
   */
  virtual void reset(bool power) = 0;

  /**
   * 追赶到指定时间：运行到该时间点之前应完成的全部点时钟。
   * @param now 目标时间（主时钟计数）；不会早于上一次追赶的时间。
   */
  virtual void catchUp(MasterTime now) = 0;

  /** 最近一次完成的一帧画面；视图在下一帧开始绘制前有效。 */
  [[nodiscard]] virtual FrameBuffer frame() const = 0;

  /** 已完成的帧数，调用方据此判断有没有新的一帧。 */
  [[nodiscard]] virtual std::uint64_t frameCount() const = 0;
};

}  // namespace ember::contracts::hardware
