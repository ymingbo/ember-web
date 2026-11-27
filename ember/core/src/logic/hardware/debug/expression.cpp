/*
 * ------------------------------------------------------------------------
 * 名称：expression.cpp
 * 说明：断点条件表达式的实现，见 expression.hpp。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-24
 * ------------------------------------------------------------------------
 */

#include "logic/hardware/debug/expression.hpp"

#include <string>
#include <utility>

#include "contracts/hardware/hardware-error.hpp"
#include "shared/errors/core-error.hpp"

namespace ember::logic::hardware {

namespace hw = contracts::hardware;

namespace {

bool isDigit(char value) { return value >= '0' && value <= '9'; }

bool isLetter(char value) { return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') || value == '_'; }

/** 一位十六进制数字的值；不是十六进制数字时返回 -1。 */
int hexValue(char value) {
  if (isDigit(value)) return value - '0';
  if (value >= 'a' && value <= 'f') return value - 'a' + 10;
  if (value >= 'A' && value <= 'F') return value - 'A' + 10;
  return -1;
}

char lower(char value) { return (value >= 'A' && value <= 'Z') ? static_cast<char>(value - 'A' + 'a') : value; }

/** 加减乘按无符号回绕，避免有符号溢出。 */
std::int64_t wrap(std::uint64_t value) { return static_cast<std::int64_t>(value); }

}  // namespace

/** 递归下降的解析器：把文本解析成 Expression 的结点数组。 */
class Expression::Parser {
 public:
  Parser(std::string_view text, std::vector<Node>& nodes) : text_(text), nodes_(nodes) {}

  /** 解析整段文本，返回根结点的下标。 */
  int parseAll() {
    const int root = parseBinary(0, 0);
    skipSpaces();
    if (position_ < text_.size()) fail("多余的内容");
    return root;
  }

 private:
  /** 最高的二元运算优先级层号（从 0 起，0 是 ||）。 */
  static constexpr int LAST_LEVEL = 9;

  [[noreturn]] void fail(const std::string& reason) const {
    throw shared::CoreError(hw::ERROR_INVALID_CONDITION, "条件表达式不合格（第 " + std::to_string(position_ + 1) + " 个字符）：" + reason);
  }

  void skipSpaces() {
    while (position_ < text_.size() && (text_[position_] == ' ' || text_[position_] == '\t')) ++position_;
  }

  [[nodiscard]] char peekChar(std::size_t ahead = 0) const { return position_ + ahead < text_.size() ? text_[position_ + ahead] : '\0'; }

  int add(Node node) {
    nodes_.push_back(node);
    return static_cast<int>(nodes_.size()) - 1;
  }

  /** 尝试在当前位置匹配指定层的二元运算符，匹配上就吃掉。 */
  std::optional<Op> matchOperator(int level) {
    const char first = peekChar();
    const char second = peekChar(1);
    const auto take = [&](std::size_t length, Op op) {
      position_ += length;
      return std::optional<Op>(op);
    };
    switch (level) {
      case 0:
        if (first == '|' && second == '|') return take(2, Op::Or);
        break;
      case 1:
        if (first == '&' && second == '&') return take(2, Op::And);
        break;
      case 2:
        if (first == '|' && second != '|') return take(1, Op::BitOr);
        break;
      case 3:
        if (first == '^') return take(1, Op::BitXor);
        break;
      case 4:
        if (first == '&' && second != '&') return take(1, Op::BitAnd);
        break;
      case 5:
        if (first == '=' && second == '=') return take(2, Op::Equal);
        if (first == '!' && second == '=') return take(2, Op::NotEqual);
        break;
      case 6:
        if (first == '<' && second == '=') return take(2, Op::LessEqual);
        if (first == '>' && second == '=') return take(2, Op::GreaterEqual);
        if (first == '<' && second != '<') return take(1, Op::Less);
        if (first == '>' && second != '>') return take(1, Op::Greater);
        break;
      case 7:
        if (first == '<' && second == '<') return take(2, Op::ShiftLeft);
        if (first == '>' && second == '>') return take(2, Op::ShiftRight);
        break;
      case 8:
        if (first == '+') return take(1, Op::Add);
        if (first == '-') return take(1, Op::Subtract);
        break;
      case 9:
        if (first == '*') return take(1, Op::Multiply);
        if (first == '/') return take(1, Op::Divide);
        if (first == '%') return take(1, Op::Modulo);
        break;
      default:
        break;
    }
    return std::nullopt;
  }

  int parseBinary(int level, int depth) {
    if (level > LAST_LEVEL) return parseUnary(depth);
    int left = parseBinary(level + 1, depth);
    for (;;) {
      skipSpaces();
      const auto op = matchOperator(level);
      if (!op.has_value()) return left;
      const int right = parseBinary(level + 1, depth);
      Node node;
      node.kind = Kind::Binary;
      node.op = *op;
      node.left = left;
      node.right = right;
      left = add(node);
    }
  }

  int parseUnary(int depth) {
    if (depth > MAX_DEPTH) fail("括号或运算符嵌套太深");
    skipSpaces();
    const char first = peekChar();
    if (first == '!' || first == '~' || first == '-') {
      ++position_;
      Node node;
      node.kind = Kind::Unary;
      node.op = first == '!' ? Op::Not : (first == '~' ? Op::BitNot : Op::Negate);
      node.left = parseUnary(depth + 1);
      return add(node);
    }
    return parsePrimary(depth);
  }

  /** 解析 [ ] 或 { } 或 ( ) 里面的表达式并吃掉右括号。 */
  int parseEnclosed(char closing, int depth) {
    const int inner = parseBinary(0, depth + 1);
    skipSpaces();
    if (peekChar() != closing) fail(std::string("缺少 ") + closing);
    ++position_;
    return inner;
  }

  int parsePrimary(int depth) {
    if (depth > MAX_DEPTH) fail("括号或运算符嵌套太深");
    skipSpaces();
    const char first = peekChar();
    if (first == '(') {
      ++position_;
      return parseEnclosed(')', depth);
    }
    if (first == '[' || first == '{') {
      ++position_;
      Node node;
      node.kind = first == '[' ? Kind::Byte : Kind::Word;
      node.left = parseEnclosed(first == '[' ? ']' : '}', depth);
      return add(node);
    }
    if (first == '$') {
      ++position_;
      return parseNumber(16);
    }
    if (isDigit(first)) {
      if (first == '0' && (lower(peekChar(1)) == 'x')) {
        position_ += 2;
        return parseNumber(16);
      }
      if (first == '0' && (lower(peekChar(1)) == 'b')) {
        position_ += 2;
        return parseNumber(2);
      }
      return parseNumber(10);
    }
    if (isLetter(first)) return parseVariable();
    fail(first == '\0' ? "表达式不完整" : std::string("无法识别的字符 ") + first);
  }

  int parseNumber(int base) {
    // 十六进制最多 16 位，二进制最多 63 位，十进制最多 18 位，保证不溢出 64 位有符号整数。
    const int maxDigits = base == 16 ? 16 : (base == 2 ? 63 : 18);
    std::uint64_t value = 0;
    int digits = 0;
    for (;;) {
      const char current = peekChar();
      const int digit = base == 16 ? hexValue(current) : (isDigit(current) ? current - '0' : -1);
      if (digit < 0 || digit >= base) break;
      if (++digits > maxDigits) fail("数值太大");
      value = value * static_cast<std::uint64_t>(base) + static_cast<std::uint64_t>(digit);
      ++position_;
    }
    if (digits == 0) fail("缺少数字");
    Node node;
    node.kind = Kind::Number;
    node.number = wrap(value);
    return add(node);
  }

  int parseVariable() {
    std::string name;
    while (isLetter(peekChar()) || isDigit(peekChar())) {
      name += lower(peekChar());
      ++position_;
    }
    Node node;
    node.kind = Kind::Variable;
    if (name == "a") node.variable = Variable::A;
    else if (name == "x") node.variable = Variable::X;
    else if (name == "y") node.variable = Variable::Y;
    else if (name == "sp") node.variable = Variable::Sp;
    else if (name == "pc") node.variable = Variable::Pc;
    else if (name == "p" || name == "status") node.variable = Variable::Status;
    else if (name == "cycles") node.variable = Variable::Cycles;
    else if (name == "frame") node.variable = Variable::Frame;
    else if (name == "scanline") node.variable = Variable::Scanline;
    else if (name == "dot") node.variable = Variable::Dot;
    else if (name == "address") node.variable = Variable::Address;
    else if (name == "value") node.variable = Variable::Value;
    else if (name == "write") node.variable = Variable::Write;
    else fail("不认识的名称 " + name);
    return add(node);
  }

  std::string_view text_;
  std::vector<Node>& nodes_;
  std::size_t position_{0};
};

Expression Expression::parse(std::string_view text) {
  if (text.empty()) throw shared::CoreError(hw::ERROR_INVALID_CONDITION, "条件表达式不能为空");
  if (text.size() > MAX_LENGTH) throw shared::CoreError(hw::ERROR_INVALID_CONDITION, "条件表达式最长 " + std::to_string(MAX_LENGTH) + " 个字节");
  Expression expression;
  Parser parser(text, expression.nodes_);
  expression.root_ = parser.parseAll();
  return expression;
}

std::optional<std::int64_t> Expression::evaluate(const ExpressionContext& context, const AccessValues& access) const { return evaluateNode(root_, context, access); }

std::optional<std::int64_t> Expression::evaluateNode(int index, const ExpressionContext& context, const AccessValues& access) const {
  const Node& node = nodes_[static_cast<std::size_t>(index)];
  switch (node.kind) {
    case Kind::Number:
      return node.number;
    case Kind::Variable: {
      switch (node.variable) {
        case Variable::A:
          return context.registers().a;
        case Variable::X:
          return context.registers().x;
        case Variable::Y:
          return context.registers().y;
        case Variable::Sp:
          return context.registers().sp;
        case Variable::Pc:
          return context.registers().pc;
        case Variable::Status:
          return context.registers().status;
        case Variable::Cycles:
          return wrap(context.registers().cycles);
        case Variable::Frame:
          return wrap(context.position().frame);
        case Variable::Scanline:
          return context.position().scanline;
        case Variable::Dot:
          return context.position().dot;
        case Variable::Address:
          return access.address;
        case Variable::Value:
          return access.value;
        case Variable::Write:
          return access.write ? 1 : 0;
      }
      return std::nullopt;
    }
    case Kind::Byte:
    case Kind::Word: {
      const auto address = evaluateNode(node.left, context, access);
      if (!address.has_value()) return std::nullopt;
      const auto low = static_cast<std::uint16_t>(static_cast<std::uint64_t>(*address) & 0xFFFFU);
      if (node.kind == Kind::Byte) return context.peek(low);
      const std::uint8_t high = context.peek(static_cast<std::uint16_t>(low + 1U));
      return static_cast<std::int64_t>(context.peek(low) | (high << 8));
    }
    case Kind::Unary: {
      const auto value = evaluateNode(node.left, context, access);
      if (!value.has_value()) return std::nullopt;
      switch (node.op) {
        case Op::Not:
          return *value == 0 ? 1 : 0;
        case Op::BitNot:
          return ~*value;
        default:
          return wrap(0U - static_cast<std::uint64_t>(*value));
      }
    }
    case Kind::Binary: {
      const auto left = evaluateNode(node.left, context, access);
      if (!left.has_value()) return std::nullopt;
      // 逻辑运算短路：左边已经能决定结果时不求右边（右边除以零也不影响结果）。
      if (node.op == Op::Or && *left != 0) return 1;
      if (node.op == Op::And && *left == 0) return 0;
      const auto right = evaluateNode(node.right, context, access);
      if (!right.has_value()) return std::nullopt;
      const std::int64_t l = *left;
      const std::int64_t r = *right;
      const auto ul = static_cast<std::uint64_t>(l);
      const auto ur = static_cast<std::uint64_t>(r);
      switch (node.op) {
        case Op::Or:
        case Op::And:
          return r != 0 ? 1 : 0;
        case Op::BitOr:
          return l | r;
        case Op::BitXor:
          return l ^ r;
        case Op::BitAnd:
          return l & r;
        case Op::Equal:
          return l == r ? 1 : 0;
        case Op::NotEqual:
          return l != r ? 1 : 0;
        case Op::Less:
          return l < r ? 1 : 0;
        case Op::LessEqual:
          return l <= r ? 1 : 0;
        case Op::Greater:
          return l > r ? 1 : 0;
        case Op::GreaterEqual:
          return l >= r ? 1 : 0;
        case Op::ShiftLeft:
          return (r < 0 || r > 63) ? 0 : wrap(ul << static_cast<unsigned>(r));
        case Op::ShiftRight:
          return (r < 0 || r > 63) ? (l < 0 ? -1 : 0) : (l >> static_cast<unsigned>(r));
        case Op::Add:
          return wrap(ul + ur);
        case Op::Subtract:
          return wrap(ul - ur);
        case Op::Multiply:
          return wrap(ul * ur);
        case Op::Divide:
          if (r == 0) return std::nullopt;
          return r == -1 ? wrap(0U - ul) : l / r;
        case Op::Modulo:
          if (r == 0) return std::nullopt;
          return r == -1 ? 0 : l % r;
        default:
          return std::nullopt;
      }
    }
  }
  return std::nullopt;
}

}  // namespace ember::logic::hardware
