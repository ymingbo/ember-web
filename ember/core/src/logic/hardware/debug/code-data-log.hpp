/*
 * ------------------------------------------------------------------------
 * 名称：code-data-log.hpp
 * 说明：代码与数据标记（CDL）：运行时给卡带 PRG-ROM 的每个字节打标记：被当作指令执行（Code）、被指令当数据读取（Data）、跳转目标、子程序入口。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * 备注：只在模拟线程使用，不加锁；主机在每条指令之前通知（onInstruction）；CPU 地址到 PRG 偏移的换算由调用方给的函数完成（按当前的分页，所以标记的是物理位置）。
 *       Data 按指令的内存操作数算（读取与读改写，不含只写的存储指令、栈与中断向量）。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "contracts/hardware/cpu.hpp"
#include "contracts/hardware/machine-debugger.hpp"
#include "logic/hardware/debug/cpu-disassembler.hpp"

namespace ember::logic::hardware {

/** 代码与数据标记。 */
class CodeDataLog {
 public:
  /**
   * 开始记录：标记清零。
   * @param size PRG-ROM 的字节数。
   * @param resolver CPU 地址换算成 PRG 偏移；不在 PRG-ROM 里时返回空。
   * @throws shared::CoreError hardware.cdlActive：已经在记录。
   */
  void start(std::size_t size, contracts::hardware::CdlResolver resolver);

  /** 停止记录，标记保留到下一次 start。没有在记录时没有效果。 */
  void stop() { active_ = false; }

  [[nodiscard]] bool active() const { return active_; }

  /** 有没有记录过（标记可以取）。 */
  [[nodiscard]] bool exists() const { return !flags_.empty(); }

  /** 每个 PRG 字节一个标记字节。 */
  [[nodiscard]] const std::vector<std::uint8_t>& flags() const { return flags_; }

  /** 即将执行 registers.pc 处的指令。 */
  void onInstruction(const contracts::hardware::CpuRegisters& registers, const PeekFunction& peek);

 private:
  void mark(std::uint16_t address, std::uint8_t flag);

  bool active_{false};
  std::vector<std::uint8_t> flags_;
  contracts::hardware::CdlResolver resolver_;
};

}  // namespace ember::logic::hardware
