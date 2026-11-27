/*
 * ------------------------------------------------------------------------
 * 名称：reconstruction-filter.hpp
 * 说明：重建滤波器：语音芯片（10 kHz）与 DAC（程序按几十千赫的节拍逐个写入）都是阶梯电平，阶梯在采样率的一半以上留下镜像成分（听起来是沙沙的杂音），
 *       用 48 kHz 下的线性相位低通（255 点 Blackman 窗 FIR）把它们滤掉，截止频率按声源的采样率取。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-16
 * 备注：线性相位，固定延迟 127 个采样（约 2.6 毫秒）；过渡带约 1 kHz，直流增益为 1。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <array>
#include <cstddef>

namespace ember::logic::hardware {

/** 48 kHz 下的低通重建滤波器。 */
class ReconstructionFilter {
 public:
  /** 滤波器的点数。 */
  static constexpr std::size_t TAPS = 255;

  /** @param cutoffHz 截止频率（赫兹），应略低于声源采样率的一半。 */
  explicit ReconstructionFilter(double cutoffHz);

  /** 清空历史。 */
  void reset();

  /** 送入一个 48 kHz 的采样，返回滤波后的采样。 */
  float process(float input);

 private:
  std::array<float, TAPS> taps_{};
  /** 两份拼接的循环历史，使最近 TAPS 个采样总是连续存放。 */
  std::array<float, 2 * TAPS> history_{};
  std::size_t position_{0};
};

}  // namespace ember::logic::hardware
