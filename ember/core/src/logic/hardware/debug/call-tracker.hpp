/*
 * ------------------------------------------------------------------------
 * 名称：call-tracker.hpp
 * 说明：调用跟踪：按 CPU 报告的 JSR、中断、RTS、RTI 维护调用栈，并在需要时统计每个子程序与中断处理程序花掉的 CPU 周期。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-24
 * 备注：只在模拟线程使用，不加锁；各主机型号共用。程序可能用 PHA 与 RTS 当跳转、或直接改栈指针，所以返回时不按“弹一帧”处理，
 *       而是按栈指针：弹出所有“调用之前的栈指针”不大于返回之后栈指针的帧（它们的返回地址已经不在栈上）；
 *       新的调用发生时，同样先弹出已经落在栈指针之上的过期帧。统计里被弹出的过期帧按此刻返回计入。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <vector>

#include "contracts/hardware/machine-debugger.hpp"

namespace ember::logic::hardware {

/** 调用跟踪。 */
class CallTracker {
 public:
  /** 调用栈的深度上限；6502 的栈只有 256 字节，正常到不了，超过时丢弃最外层的帧。 */
  static constexpr std::size_t MAX_DEPTH = 256;

  /**
   * 进入子程序或中断处理程序（JSR 执行完毕，或 CPU 完成了中断序列）。
   * @param stackPointer 调用完成后的栈指针。
   * @param stackBytes 调用压入栈的字节数（JSR 为 2，中断为 3）。
   * @param cycles 此刻的 CPU 周期数。
   */
  void enter(contracts::hardware::CallKind kind, std::uint16_t callAddress, std::uint16_t targetAddress, std::uint16_t returnAddress, std::uint8_t stackPointer, int stackBytes, std::uint64_t cycles);

  /**
   * RTS 或 RTI 执行完毕。
   * @param stackPointer 返回之后的栈指针。
   */
  void leave(std::uint8_t stackPointer, std::uint64_t cycles);

  /** 复位开始（周期数还是复位之前的）：还在栈上的帧按此刻返回计入统计，统计在进行时把到此刻为止的周期累计起来。 */
  void beginReset(std::uint64_t cycles);

  /** 复位序列执行完毕：调用栈清空；统计在进行时，统计的时间起点移到现在。 */
  void reset(std::uint64_t cycles);

  /** 调用栈，最里层的在前。 */
  [[nodiscard]] std::vector<contracts::hardware::CallFrame> stack() const;

  /** 开始统计：统计清零，已经在栈上的帧从现在起计时。 */
  void startProfile(std::uint64_t cycles);

  /** 停止统计，结果保留到下一次 startProfile。 */
  void stopProfile(std::uint64_t cycles);

  /** 统计的当前结果；没有开始过时为空。 */
  [[nodiscard]] std::optional<contracts::hardware::ProfileReport> profile(std::uint64_t cycles) const;

 private:
  struct Frame {
    contracts::hardware::CallFrame info;
    /** 调用之前的栈指针：返回地址压在它之下的 stackBytes 个字节里。 */
    int stackBefore{0};
    /** 统计用：进入的周期数，与已经返回的下层调用占去的周期数。 */
    std::uint64_t entryCycles{0};
    std::uint64_t childCycles{0};
  };

  /** 弹出最里层的一帧；统计在进行时计入。 */
  void closeTop(std::uint64_t cycles);

  /** 按当前的栈与已有的统计算出结果；未返回的帧按此刻返回计，调用次数各加一次。 */
  [[nodiscard]] contracts::hardware::ProfileReport buildReport(std::uint64_t cycles, bool active) const;

  std::vector<Frame> frames_;
  bool profiling_{false};
  /** 已经有过统计结果（进行中或已停止）。 */
  bool hasProfile_{false};
  /** 按入口地址累计的统计；不含还在栈上的帧与根。 */
  std::map<std::uint16_t, contracts::hardware::ProfileEntry> table_;
  /** 根（不在任何子程序里的代码）：进入统计的周期数与已返回的顶层调用占去的周期数。 */
  std::uint64_t rootEntryCycles_{0};
  std::uint64_t rootChildCycles_{0};
  /** 复位之前已经统计过的周期数。 */
  std::uint64_t accumulatedCycles_{0};
  /** 停止时算好的结果。 */
  std::optional<contracts::hardware::ProfileReport> stopped_;
};

}  // namespace ember::logic::hardware
