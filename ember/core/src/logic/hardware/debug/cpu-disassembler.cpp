/*
 * ------------------------------------------------------------------------
 * 名称：cpu-disassembler.cpp
 * 说明：6502 反汇编的实现，见 cpu-disassembler.hpp。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * ------------------------------------------------------------------------
 */

#include "logic/hardware/debug/cpu-disassembler.hpp"

#include <cstdio>
#include <string_view>

namespace ember::logic::hardware {

namespace hw = contracts::hardware;

namespace {

/** 寻址方式。 */
enum class Mode : std::uint8_t { Imp, Acc, Imm, Zp, Zpx, Zpy, Abs, Abx, Aby, Ind, Izx, Izy, Rel };

constexpr Mode IMP = Mode::Imp;
constexpr Mode ACC = Mode::Acc;
constexpr Mode IMM = Mode::Imm;
constexpr Mode ZP = Mode::Zp;
constexpr Mode ZPX = Mode::Zpx;
constexpr Mode ZPY = Mode::Zpy;
constexpr Mode ABS = Mode::Abs;
constexpr Mode ABX = Mode::Abx;
constexpr Mode ABY = Mode::Aby;
constexpr Mode IND = Mode::Ind;
constexpr Mode IZX = Mode::Izx;
constexpr Mode IZY = Mode::Izy;
constexpr Mode REL = Mode::Rel;

struct Entry {
  /** 助记符；以 * 开头的是非官方指令。 */
  const char* name;
  Mode mode;
};

/** 按操作码排列的指令表，每行 16 项。 */
constexpr Entry TABLE[256] = {
    {"BRK", IMP}, {"ORA", IZX}, {"*KIL", IMP}, {"*SLO", IZX}, {"*NOP", ZP},  {"ORA", ZP},  {"ASL", ZP},  {"*SLO", ZP},  {"PHP", IMP}, {"ORA", IMM}, {"ASL", ACC},  {"*ANC", IMM}, {"*NOP", ABS},  {"ORA", ABS},  {"ASL", ABS},  {"*SLO", ABS},
    {"BPL", REL}, {"ORA", IZY}, {"*KIL", IMP}, {"*SLO", IZY}, {"*NOP", ZPX}, {"ORA", ZPX}, {"ASL", ZPX}, {"*SLO", ZPX}, {"CLC", IMP}, {"ORA", ABY}, {"*NOP", IMP}, {"*SLO", ABY}, {"*NOP", ABX},  {"ORA", ABX},  {"ASL", ABX},  {"*SLO", ABX},
    {"JSR", ABS}, {"AND", IZX}, {"*KIL", IMP}, {"*RLA", IZX}, {"BIT", ZP},   {"AND", ZP},  {"ROL", ZP},  {"*RLA", ZP},  {"PLP", IMP}, {"AND", IMM}, {"ROL", ACC},  {"*ANC", IMM}, {"BIT", ABS},   {"AND", ABS},  {"ROL", ABS},  {"*RLA", ABS},
    {"BMI", REL}, {"AND", IZY}, {"*KIL", IMP}, {"*RLA", IZY}, {"*NOP", ZPX}, {"AND", ZPX}, {"ROL", ZPX}, {"*RLA", ZPX}, {"SEC", IMP}, {"AND", ABY}, {"*NOP", IMP}, {"*RLA", ABY}, {"*NOP", ABX},  {"AND", ABX},  {"ROL", ABX},  {"*RLA", ABX},
    {"RTI", IMP}, {"EOR", IZX}, {"*KIL", IMP}, {"*SRE", IZX}, {"*NOP", ZP},  {"EOR", ZP},  {"LSR", ZP},  {"*SRE", ZP},  {"PHA", IMP}, {"EOR", IMM}, {"LSR", ACC},  {"*ALR", IMM}, {"JMP", ABS},   {"EOR", ABS},  {"LSR", ABS},  {"*SRE", ABS},
    {"BVC", REL}, {"EOR", IZY}, {"*KIL", IMP}, {"*SRE", IZY}, {"*NOP", ZPX}, {"EOR", ZPX}, {"LSR", ZPX}, {"*SRE", ZPX}, {"CLI", IMP}, {"EOR", ABY}, {"*NOP", IMP}, {"*SRE", ABY}, {"*NOP", ABX},  {"EOR", ABX},  {"LSR", ABX},  {"*SRE", ABX},
    {"RTS", IMP}, {"ADC", IZX}, {"*KIL", IMP}, {"*RRA", IZX}, {"*NOP", ZP},  {"ADC", ZP},  {"ROR", ZP},  {"*RRA", ZP},  {"PLA", IMP}, {"ADC", IMM}, {"ROR", ACC},  {"*ARR", IMM}, {"JMP", IND},   {"ADC", ABS},  {"ROR", ABS},  {"*RRA", ABS},
    {"BVS", REL}, {"ADC", IZY}, {"*KIL", IMP}, {"*RRA", IZY}, {"*NOP", ZPX}, {"ADC", ZPX}, {"ROR", ZPX}, {"*RRA", ZPX}, {"SEI", IMP}, {"ADC", ABY}, {"*NOP", IMP}, {"*RRA", ABY}, {"*NOP", ABX},  {"ADC", ABX},  {"ROR", ABX},  {"*RRA", ABX},
    {"*NOP", IMM}, {"STA", IZX}, {"*NOP", IMM}, {"*SAX", IZX}, {"STY", ZP},  {"STA", ZP},  {"STX", ZP},  {"*SAX", ZP},  {"DEY", IMP}, {"*NOP", IMM}, {"TXA", IMP}, {"*XAA", IMM}, {"STY", ABS},   {"STA", ABS},  {"STX", ABS},  {"*SAX", ABS},
    {"BCC", REL}, {"STA", IZY}, {"*KIL", IMP}, {"*AHX", IZY}, {"STY", ZPX},  {"STA", ZPX}, {"STX", ZPY}, {"*SAX", ZPY}, {"TYA", IMP}, {"STA", ABY}, {"TXS", IMP},  {"*TAS", ABY}, {"*SHY", ABX},  {"STA", ABX},  {"*SHX", ABY},  {"*AHX", ABY},
    {"LDY", IMM}, {"LDA", IZX}, {"LDX", IMM},  {"*LAX", IZX}, {"LDY", ZP},   {"LDA", ZP},  {"LDX", ZP},  {"*LAX", ZP},  {"TAY", IMP}, {"LDA", IMM}, {"TAX", IMP},  {"*LAX", IMM}, {"LDY", ABS},   {"LDA", ABS},  {"LDX", ABS},  {"*LAX", ABS},
    {"BCS", REL}, {"LDA", IZY}, {"*KIL", IMP}, {"*LAX", IZY}, {"LDY", ZPX},  {"LDA", ZPX}, {"LDX", ZPY}, {"*LAX", ZPY}, {"CLV", IMP}, {"LDA", ABY}, {"TSX", IMP},  {"*LAS", ABY}, {"LDY", ABX},   {"LDA", ABX},  {"LDX", ABY},  {"*LAX", ABY},
    {"CPY", IMM}, {"CMP", IZX}, {"*NOP", IMM}, {"*DCP", IZX}, {"CPY", ZP},   {"CMP", ZP},  {"DEC", ZP},  {"*DCP", ZP},  {"INY", IMP}, {"CMP", IMM}, {"DEX", IMP},  {"*AXS", IMM}, {"CPY", ABS},   {"CMP", ABS},  {"DEC", ABS},  {"*DCP", ABS},
    {"BNE", REL}, {"CMP", IZY}, {"*KIL", IMP}, {"*DCP", IZY}, {"*NOP", ZPX}, {"CMP", ZPX}, {"DEC", ZPX}, {"*DCP", ZPX}, {"CLD", IMP}, {"CMP", ABY}, {"*NOP", IMP}, {"*DCP", ABY}, {"*NOP", ABX},  {"CMP", ABX},  {"DEC", ABX},  {"*DCP", ABX},
    {"CPX", IMM}, {"SBC", IZX}, {"*NOP", IMM}, {"*ISB", IZX}, {"CPX", ZP},   {"SBC", ZP},  {"INC", ZP},  {"*ISB", ZP},  {"INX", IMP}, {"SBC", IMM}, {"NOP", IMP},  {"*SBC", IMM}, {"CPX", ABS},   {"SBC", ABS},  {"INC", ABS},  {"*ISB", ABS},
    {"BEQ", REL}, {"SBC", IZY}, {"*KIL", IMP}, {"*ISB", IZY}, {"*NOP", ZPX}, {"SBC", ZPX}, {"INC", ZPX}, {"*ISB", ZPX}, {"SED", IMP}, {"SBC", ABY}, {"*NOP", IMP}, {"*ISB", ABY}, {"*NOP", ABX},  {"SBC", ABX},  {"INC", ABX},  {"*ISB", ABX},
};

std::uint8_t lengthOf(Mode mode) {
  switch (mode) {
    case Mode::Imp:
    case Mode::Acc:
      return 1;
    case Mode::Abs:
    case Mode::Abx:
    case Mode::Aby:
    case Mode::Ind:
      return 3;
    default:
      return 2;
  }
}

/** 按格式写成文本。 */
template <typename... Args>
std::string format(const char* pattern, Args... args) {
  char buffer[96];
  std::snprintf(buffer, sizeof(buffer), pattern, args...);
  return buffer;
}

std::uint16_t word(std::uint8_t low, std::uint8_t high) { return static_cast<std::uint16_t>(low | (high << 8)); }

}  // namespace

/** 只写内存、不读的指令。 */
static bool isStore(const char* name) {
  const std::string_view view(name);
  return view == "STA" || view == "STX" || view == "STY" || view == "SAX" || view == "AHX" || view == "SHX" || view == "SHY" || view == "TAS";
}

Disassembly disassemble(std::uint16_t pc, const hw::CpuRegisters& registers, const PeekFunction& peek, bool withText) {
  const std::uint8_t opcode = peek(pc);
  const Entry& entry = TABLE[opcode];
  Disassembly result;
  result.length = lengthOf(entry.mode);
  result.unofficial = entry.name[0] == '*';
  const char* name = entry.name + (result.unofficial ? 1 : 0);
  const std::uint8_t operand1 = result.length > 1 ? peek(static_cast<std::uint16_t>(pc + 1)) : 0;
  const std::uint8_t operand2 = result.length > 2 ? peek(static_cast<std::uint16_t>(pc + 2)) : 0;
  const std::uint16_t absolute = word(operand1, operand2);
  const bool jump = std::string_view(name) == "JMP";
  const bool call = std::string_view(name) == "JSR";
  // 有内存操作数的指令的有效地址；跳转的目标另算。
  std::optional<std::uint16_t> effective;

  switch (entry.mode) {
    case Mode::Imp:
      if (withText) result.text = name;
      break;
    case Mode::Acc:
      if (withText) result.text = format("%s A", name);
      break;
    case Mode::Imm:
      if (withText) result.text = format("%s #$%02X", name, operand1);
      break;
    case Mode::Zp:
      effective = operand1;
      if (withText) result.text = format("%s $%02X = %02X", name, operand1, peek(operand1));
      break;
    case Mode::Zpx: {
      effective = static_cast<std::uint8_t>(operand1 + registers.x);
      if (withText) result.text = format("%s $%02X,X @ %02X = %02X", name, operand1, *effective, peek(*effective));
      break;
    }
    case Mode::Zpy: {
      effective = static_cast<std::uint8_t>(operand1 + registers.y);
      if (withText) result.text = format("%s $%02X,Y @ %02X = %02X", name, operand1, *effective, peek(*effective));
      break;
    }
    case Mode::Abs:
      if (jump || call) {
        result.flow = jump ? Flow::Jump : Flow::Call;
        result.target = absolute;
        if (withText) result.text = format("%s $%04X", name, absolute);
      } else {
        effective = absolute;
        if (withText) result.text = format("%s $%04X = %02X", name, absolute, peek(absolute));
      }
      break;
    case Mode::Abx: {
      effective = static_cast<std::uint16_t>(absolute + registers.x);
      if (withText) result.text = format("%s $%04X,X @ %04X = %02X", name, absolute, *effective, peek(*effective));
      break;
    }
    case Mode::Aby: {
      effective = static_cast<std::uint16_t>(absolute + registers.y);
      if (withText) result.text = format("%s $%04X,Y @ %04X = %02X", name, absolute, *effective, peek(*effective));
      break;
    }
    case Mode::Ind: {
      // 6502 的间接跳转读指针高字节时不跨页。
      const auto highAddress = static_cast<std::uint16_t>((absolute & 0xFF00U) | ((absolute + 1U) & 0x00FFU));
      result.flow = Flow::Jump;
      result.target = word(peek(absolute), peek(highAddress));
      result.dataAddress = absolute;
      if (withText) result.text = format("%s ($%04X) = %04X", name, absolute, result.target);
      break;
    }
    case Mode::Izx: {
      const auto pointer = static_cast<std::uint8_t>(operand1 + registers.x);
      effective = word(peek(pointer), peek(static_cast<std::uint8_t>(pointer + 1)));
      if (withText) result.text = format("%s ($%02X,X) @ %02X = %04X = %02X", name, operand1, pointer, *effective, peek(*effective));
      break;
    }
    case Mode::Izy: {
      const std::uint16_t base = word(peek(operand1), peek(static_cast<std::uint8_t>(operand1 + 1)));
      effective = static_cast<std::uint16_t>(base + registers.y);
      if (withText) result.text = format("%s ($%02X),Y = %04X @ %04X = %02X", name, operand1, base, *effective, peek(*effective));
      break;
    }
    case Mode::Rel: {
      result.flow = Flow::Branch;
      result.target = static_cast<std::uint16_t>(pc + 2 + static_cast<std::int8_t>(operand1));
      // 条件分支的操作码：位 7-6 选标志（N、V、C、Z），位 5 是要求这个标志为 1 还是 0。
      constexpr std::uint8_t FLAGS[4] = {0x80, 0x40, 0x01, 0x02};
      const bool flagSet = (registers.status & FLAGS[(opcode >> 6) & 3]) != 0;
      result.taken = flagSet == (((opcode >> 5) & 1) != 0);
      if (withText) result.text = format("%s $%04X", name, result.target);
      break;
    }
  }
  if (effective.has_value() && !isStore(name)) result.dataAddress = effective;
  return result;
}

}  // namespace ember::logic::hardware
