/*
 * ------------------------------------------------------------------------
 * 名称：keyboard-sb2000.hpp
 * 说明：小霸王学习机的键盘：物理键位（KeyboardEvent.code）到扫描码的映射，以及键盘与主机之间的字节协议（命令、应答与按键报告）。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-15
 * 备注：扫描码按 IBM 码集 1 的按下码，与 DirectInput 的 DIK 值一致：不小于 $80 的是扩展键，先发 $E0 再发低 7 位（松开时第二个字节带 $80）；
 *       命令都先回应答 $FA（延迟约 2 毫秒）；$FF 复位再回 $AA（约 100 毫秒后）；$F2 读 ID 回 $12、$34；
 *       $ED、$F0、$F3、$FB、$FC、$FD 带一个选项字节；$F0 选项不为 0 时键盘开始工作（每 20 毫秒检查一次按键变化，最多报告一个）；
 *       键盘不工作时不报告按键；协议细节没有在真机键盘上测过。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <optional>
#include <string_view>

#include "contracts/hardware/scheduler.hpp"
#include "contracts/hardware/serial-device.hpp"

namespace ember::logic::hardware {

/** 小霸王键盘。 */
class KeyboardSb2000 final : public contracts::hardware::SerialDevice, public contracts::hardware::EventListener {
 public:
  /**
   * @param scheduler 调度器，应答与报告的时间靠它的事件。
   * @param millisecondTicks 1 毫秒对应的主时钟周期数。
   */
  KeyboardSb2000(contracts::hardware::Scheduler& scheduler, contracts::hardware::MasterTime millisecondTicks);

  void connect(contracts::hardware::ByteReceiver* receiver) override { receiver_ = receiver; }
  void sendByte(std::uint8_t value) override;
  void pulseControl() override {}
  void reset(bool power) override;
  void onEvent(contracts::hardware::MasterTime when, std::uint32_t tag) override;

  /**
   * 设置一个键的状态。
   * @param scanCode DirectInput 的 DIK 值（$80 以上是扩展键）。
   * @param down true 为按下。
   */
  void setKey(std::uint8_t scanCode, bool down);

  /** 物理键位（KeyboardEvent.code）对应的 DIK 扫描码；没有对应的键返回空。 */
  [[nodiscard]] static std::optional<std::uint8_t> scanCodeFor(std::string_view code);

  /** 键盘是否已被主机选定扫描码集而开始工作。 */
  [[nodiscard]] bool working() const { return working_; }

 private:
  /** 待发出的字节与它相对上一个字节的延迟。 */
  struct Pending {
    std::uint8_t value;
    contracts::hardware::MasterTime delay;
  };

  static constexpr std::uint32_t TAG_SEND = 0;
  static constexpr std::uint32_t TAG_REPORT = 1;

  /** 把字节排进发送队列。 */
  void enqueue(std::uint8_t value, contracts::hardware::MasterTime delay);
  /** 队列有内容而没有登记事件时，登记队首的发送事件。 */
  void pump();
  /** 每 20 毫秒检查一次按键变化。 */
  void report();
  /** 处理一个命令字节或选项字节。 */
  void handleCommand(std::uint8_t value);

  contracts::hardware::Scheduler& scheduler_;
  contracts::hardware::MasterTime ms_;
  contracts::hardware::ByteReceiver* receiver_{nullptr};
  std::deque<Pending> queue_;
  contracts::hardware::EventId sendEvent_{contracts::hardware::NO_EVENT};
  contracts::hardware::EventId reportEvent_{contracts::hardware::NO_EVENT};
  bool working_{false};
  bool waitingOption_{false};
  std::uint8_t command_{0};
  std::uint8_t ledStatus_{0};
  std::uint8_t scanCodeSet_{2};
  std::array<bool, 256> status_{};
  std::array<bool, 256> reported_{};
};

}  // namespace ember::logic::hardware
