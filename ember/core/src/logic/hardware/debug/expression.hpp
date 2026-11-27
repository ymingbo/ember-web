/*
 * ------------------------------------------------------------------------
 * 名称：expression.hpp
 * 说明：断点条件表达式：解析一段文本，在断点命中时按 CPU 寄存器、总线上的内存与扫描位置求值。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-24
 * 备注：语法见 docs/04 第 4.5 节；全部按 64 位有符号整数运算，加减乘与移位按无符号回绕，除以零或对零取余时求值失败（调用方当作条件不成立）；
 *       读内存只经无副作用的读取。解析是纯文本处理，嵌套深度与长度都有上限。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "contracts/hardware/cpu.hpp"
#include "contracts/hardware/ppu.hpp"

namespace ember::logic::hardware {

/** 表达式求值时能读到的主机状态。 */
class ExpressionContext {
 public:
  virtual ~ExpressionContext() = default;

  /** 当前的 CPU 寄存器与周期数。 */
  [[nodiscard]] virtual contracts::hardware::CpuRegisters registers() const = 0;

  /** 无副作用地读 CPU 总线上的一个字节。 */
  [[nodiscard]] virtual std::uint8_t peek(std::uint16_t address) const = 0;

  /** 当前的扫描位置。 */
  [[nodiscard]] virtual contracts::hardware::VideoPosition position() const = 0;
};

/** 读写断点命中时才有意义的变量；执行断点命中时 address 是将要执行的指令地址，value 与 write 为 0。 */
struct AccessValues {
  std::uint16_t address{0};
  std::uint8_t value{0};
  bool write{false};
};

/** 一个解析好的表达式。 */
class Expression {
 public:
  /** 表达式文本的长度上限（字节）。 */
  static constexpr std::size_t MAX_LENGTH = 256;
  /** 括号与取值嵌套的层数上限。 */
  static constexpr int MAX_DEPTH = 32;

  /**
   * 解析表达式。
   * @throws shared::CoreError hardware.invalidCondition：文本为空、太长、写法错误、嵌套太深。
   */
  static Expression parse(std::string_view text);

  /**
   * 求值。
   * @return 求值结果；除以零或对零取余时为空。
   */
  [[nodiscard]] std::optional<std::int64_t> evaluate(const ExpressionContext& context, const AccessValues& access) const;

 private:
  /** 结点的种类。 */
  enum class Kind : std::uint8_t { Number, Variable, Byte, Word, Unary, Binary };

  /** 变量。 */
  enum class Variable : std::uint8_t { A, X, Y, Sp, Pc, Status, Cycles, Frame, Scanline, Dot, Address, Value, Write };

  /** 运算符；一元运算符是 Not、BitNot、Negate。 */
  enum class Op : std::uint8_t { Not, BitNot, Negate, Or, And, BitOr, BitXor, BitAnd, Equal, NotEqual, Less, LessEqual, Greater, GreaterEqual, ShiftLeft, ShiftRight, Add, Subtract, Multiply, Divide, Modulo };

  struct Node {
    Kind kind{Kind::Number};
    std::int64_t number{0};
    Variable variable{Variable::A};
    Op op{Op::Add};
    /** 子结点在 nodes_ 里的下标；Byte 与 Word 的地址表达式、一元运算的操作数放在 left。 */
    int left{-1};
    int right{-1};
  };

  class Parser;

  [[nodiscard]] std::optional<std::int64_t> evaluateNode(int index, const ExpressionContext& context, const AccessValues& access) const;

  std::vector<Node> nodes_;
  int root_{-1};
};

}  // namespace ember::logic::hardware
