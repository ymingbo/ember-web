/*
 * ------------------------------------------------------------------------
 * 名称：mouse-sb2000.hpp
 * 说明：小霸王学习机的串口鼠标：微软鼠标协议（1200bps，3 字节数据包），RTS 复位后先回 PnP 标识，之后每次移动或按键变化报告一个数据包。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-15
 * 备注：RTS（扩展 I/O 位 1 的上升沿）之后约 15 毫秒开始发 PnP 标识（每字节间隔约 5 毫秒），并进入工作状态；
 *       数据包：第 1 字节 $40 | 右键<<4 | 左键<<5 | Y 的位 7-6 <<2 | X 的位 7-6，第 2 字节 X 的低 6 位，第 3 字节 Y 的低 6 位；
 *       位移累加到发出时取走（限制在 -128 到 127），数据包的 3 个字节间隔约 5 毫秒；上一个数据包没发完时，新的位移留到发完后再报告；
 *       鼠标没有工作（没有收到 RTS）时不报告。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <cstdint>
#include <deque>

#include "contracts/hardware/scheduler.hpp"
#include "contracts/hardware/serial-device.hpp"

namespace ember::logic::hardware {

/** 小霸王串口鼠标。 */
class MouseSb2000 final : public contracts::hardware::SerialDevice, public contracts::hardware::EventListener {
 public:
  /** 按键位：1 左键、2 右键、4 中键（同 MouseInput 的 buttons）。 */
  static constexpr std::uint8_t BUTTON_LEFT = 1;
  static constexpr std::uint8_t BUTTON_RIGHT = 2;

  /**
   * @param scheduler 调度器，字节发出的时间靠它的事件。
   * @param millisecondTicks 1 毫秒对应的主时钟周期数。
   */
  MouseSb2000(contracts::hardware::Scheduler& scheduler, contracts::hardware::MasterTime millisecondTicks);

  void connect(contracts::hardware::ByteReceiver* receiver) override { receiver_ = receiver; }
  void sendByte(std::uint8_t /*value*/) override {}
  void pulseControl() override;
  void reset(bool power) override;
  void onEvent(contracts::hardware::MasterTime when, std::uint32_t tag) override;

  /**
   * 报告一次鼠标动作。
   * @param dx 横向位移（累加），单位是鼠标的计数。
   * @param dy 纵向位移（累加）。
   * @param buttons 当前按住的按键位。
   */
  void move(int dx, int dy, std::uint8_t buttons);

  /** 是否已经收到 RTS 而开始工作。 */
  [[nodiscard]] bool working() const { return working_; }

 private:
  struct Pending {
    std::uint8_t value;
    contracts::hardware::MasterTime delay;
  };

  void enqueue(std::uint8_t value, contracts::hardware::MasterTime delay);
  void pump();
  /** 有累积的位移或按键变化，且没有字节在发送时，发出一个数据包。 */
  void reportIfIdle();

  contracts::hardware::Scheduler& scheduler_;
  contracts::hardware::MasterTime ms_;
  contracts::hardware::ByteReceiver* receiver_{nullptr};
  std::deque<Pending> queue_;
  contracts::hardware::EventId eventId_{contracts::hardware::NO_EVENT};
  bool working_{false};
  int pendingX_{0};
  int pendingY_{0};
  std::uint8_t buttons_{0};
  std::uint8_t reportedButtons_{0};
};

}  // namespace ember::logic::hardware
