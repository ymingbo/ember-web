/*
 * ------------------------------------------------------------------------
 * 名称：mouse-bbg.hpp
 * 说明：步步高鼠标：经键盘控制芯片的串行协议接在 $4016、$4017 上，读 $4016 连续 9 次后接通，主机发命令、鼠标回应答与 3 字节数据。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-16
 * 备注：接口只有三个动作：读 $4016（noteStatusRead）、写 $4016（write）、读 $4017 的位 0（readLine）；
 *       接通：读 $4016 连续 9 次（中间没有读 $4017 与写 $4016）后接通，此后读 $4017 位 0 恒为 1（空闲）；
 *       命令：接通后写 $05 进入收命令，之后每次写入的位 2 为 1 时取位 0 的反相作为命令的下一位（低位在前），收满 22 位后命令结束，命令字节取低 8 位；
 *       收命令期间每读一次 $4017，位 0 在 1 与 0 之间交替；
 *       应答：命令结束后回应一帧 $FA；命令 $EB（读数据）再依次回 3 个数据字节：按键与符号、纵向位移、横向位移（横向位移取反）；命令 $F0（设置远程模式）只有应答；其他命令也只有应答；
 *       一帧 32 位，按位的先后每读一次 $4017 位 0 给出一位：起始 4 位 1、1、0、1，之后每个数据位 3 位（数据位取反、0、1），第 9 个数据位位置给出奇偶，最后补 0 与 1；
 *       一帧发完后的下一次读是帧间隔（位 0 为 0），帧间隔之后发下一帧，全部发完后鼠标回到未接通；
 *       位移的单位是屏幕像素，在两次读数据命令之间累加；超过 -256 到 255 的位移取 0 并置溢出位。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <array>
#include <cstdint>

namespace ember::logic::hardware {

/** 步步高鼠标。 */
class MouseBbg final {
 public:
  /** 按键位：左键、右键、中键（读数据命令的第 1 个字节里的位）。 */
  static constexpr std::uint8_t BUTTON_RIGHT = 0x01;
  static constexpr std::uint8_t BUTTON_MIDDLE = 0x02;
  static constexpr std::uint8_t BUTTON_LEFT = 0x04;

  /** 回到未接通（上电与复位时）；位移与按键清零。 */
  void reset();

  /** 读 $4016 一次。 */
  void noteStatusRead();

  /** 写 $4016。 */
  void write(std::uint8_t value);

  /** 读 $4017 的位 0（0 或 1）。 */
  [[nodiscard]] std::uint8_t readLine();

  /**
   * 鼠标移动或按键变化。
   * @param dx 横向位移（屏幕像素，向右为正），累加到下一次读数据命令。
   * @param dy 纵向位移（向下为正），同上。
   * @param buttons 当前按住的按键位（BUTTON_*）。
   */
  void move(int dx, int dy, std::uint8_t buttons);

  /** 是否已接通，供测试。 */
  [[nodiscard]] bool active() const { return active_; }

 private:
  enum class State : std::uint8_t { Idle, ReceiveCommand, Transmit };

  /** 把一个字节编成 32 位帧。 */
  [[nodiscard]] static std::uint32_t encodeFrame(std::uint8_t data);
  /** 命令收满：准备应答。 */
  void finishCommand();

  bool active_{false};
  State state_{State::Idle};
  /** 读 $4016 的连续次数。 */
  unsigned statusReads_{0};
  /** 收命令时已收的位数与命令值。 */
  unsigned commandBits_{0};
  std::uint32_t command_{0};
  /** 收命令时读 $4017 的次数，发送时已发的位数。 */
  unsigned lineReads_{0};
  std::uint32_t frame_{0};
  /** 还有几帧要发（含正在发的这一帧）。 */
  unsigned framesLeft_{0};
  /** 读数据命令的 3 个字节。 */
  std::array<std::uint8_t, 3> data_{};
  int deltaX_{0};
  int deltaY_{0};
  std::uint8_t buttons_{0};
};

}  // namespace ember::logic::hardware
