/*
 * ------------------------------------------------------------------------
 * 名称：opcode-table.hpp
 * 说明：经典 FC 的 6502 操作码表：256 个操作码各自的操作与寻址方式，只在 cpu-6502 模块内部使用。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * 备注：包含全部官方指令，以及 nestest 用到的非官方指令（LAX、SAX、DCP、ISB、SLO、RLA、SRE、RRA 与各种 NOP 变体、
 *       $EB 的 SBC）和 $AB 的 LAX 立即数（A 与 X 都取立即数，学习机的引导程序用到它）；
 *       表里其余的操作码按 2 周期的单字节 NOP 处理。十进制模式由 CPU 核按设置决定。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <array>
#include <cstdint>

namespace ember::logic::hardware {

/** 操作。 */
enum class Op : std::uint8_t {
  Adc, And, Asl, Bcc, Bcs, Beq, Bit, Bmi, Bne, Bpl, Brk, Bvc, Bvs, Clc, Cld, Cli, Clv, Cmp, Cpx, Cpy, Dec, Dex, Dey, Eor, Inc, Inx, Iny, Jmp, Jsr,
  Lda, Ldx, Ldy, Lsr, Nop, Ora, Pha, Php, Pla, Plp, Rol, Ror, Rti, Rts, Sbc, Sec, Sed, Sei, Sta, Stx, Sty, Tax, Tay, Tsx, Txa, Txs, Tya,
  // 非官方
  Lax, Sax, Dcp, Isb, Slo, Rla, Sre, Rra
};

/** 寻址方式。 */
enum class Mode : std::uint8_t { Implied, Accumulator, Immediate, ZeroPage, ZeroPageX, ZeroPageY, Absolute, AbsoluteX, AbsoluteY, IndirectX, IndirectY, Indirect, Relative };

/** 一个操作码的操作与寻址方式。 */
struct OpcodeInfo {
  Op op;
  Mode mode;
};

namespace detail {

/** 生成 256 项操作码表，按操作码的高四位分行，每行 16 项。 */
constexpr std::array<OpcodeInfo, 256> makeOpcodeTable() {
  using enum Op;
  using enum Mode;
  constexpr OpcodeInfo UNDEF{Nop, Implied};
  return {{
      // $00
      {Brk, Implied}, {Ora, IndirectX}, UNDEF, {Slo, IndirectX}, {Nop, ZeroPage}, {Ora, ZeroPage}, {Asl, ZeroPage}, {Slo, ZeroPage},
      {Php, Implied}, {Ora, Immediate}, {Asl, Accumulator}, UNDEF, {Nop, Absolute}, {Ora, Absolute}, {Asl, Absolute}, {Slo, Absolute},
      // $10
      {Bpl, Relative}, {Ora, IndirectY}, UNDEF, {Slo, IndirectY}, {Nop, ZeroPageX}, {Ora, ZeroPageX}, {Asl, ZeroPageX}, {Slo, ZeroPageX},
      {Clc, Implied}, {Ora, AbsoluteY}, {Nop, Implied}, {Slo, AbsoluteY}, {Nop, AbsoluteX}, {Ora, AbsoluteX}, {Asl, AbsoluteX}, {Slo, AbsoluteX},
      // $20
      {Jsr, Absolute}, {And, IndirectX}, UNDEF, {Rla, IndirectX}, {Bit, ZeroPage}, {And, ZeroPage}, {Rol, ZeroPage}, {Rla, ZeroPage},
      {Plp, Implied}, {And, Immediate}, {Rol, Accumulator}, UNDEF, {Bit, Absolute}, {And, Absolute}, {Rol, Absolute}, {Rla, Absolute},
      // $30
      {Bmi, Relative}, {And, IndirectY}, UNDEF, {Rla, IndirectY}, {Nop, ZeroPageX}, {And, ZeroPageX}, {Rol, ZeroPageX}, {Rla, ZeroPageX},
      {Sec, Implied}, {And, AbsoluteY}, {Nop, Implied}, {Rla, AbsoluteY}, {Nop, AbsoluteX}, {And, AbsoluteX}, {Rol, AbsoluteX}, {Rla, AbsoluteX},
      // $40
      {Rti, Implied}, {Eor, IndirectX}, UNDEF, {Sre, IndirectX}, {Nop, ZeroPage}, {Eor, ZeroPage}, {Lsr, ZeroPage}, {Sre, ZeroPage},
      {Pha, Implied}, {Eor, Immediate}, {Lsr, Accumulator}, UNDEF, {Jmp, Absolute}, {Eor, Absolute}, {Lsr, Absolute}, {Sre, Absolute},
      // $50
      {Bvc, Relative}, {Eor, IndirectY}, UNDEF, {Sre, IndirectY}, {Nop, ZeroPageX}, {Eor, ZeroPageX}, {Lsr, ZeroPageX}, {Sre, ZeroPageX},
      {Cli, Implied}, {Eor, AbsoluteY}, {Nop, Implied}, {Sre, AbsoluteY}, {Nop, AbsoluteX}, {Eor, AbsoluteX}, {Lsr, AbsoluteX}, {Sre, AbsoluteX},
      // $60
      {Rts, Implied}, {Adc, IndirectX}, UNDEF, {Rra, IndirectX}, {Nop, ZeroPage}, {Adc, ZeroPage}, {Ror, ZeroPage}, {Rra, ZeroPage},
      {Pla, Implied}, {Adc, Immediate}, {Ror, Accumulator}, UNDEF, {Jmp, Indirect}, {Adc, Absolute}, {Ror, Absolute}, {Rra, Absolute},
      // $70
      {Bvs, Relative}, {Adc, IndirectY}, UNDEF, {Rra, IndirectY}, {Nop, ZeroPageX}, {Adc, ZeroPageX}, {Ror, ZeroPageX}, {Rra, ZeroPageX},
      {Sei, Implied}, {Adc, AbsoluteY}, {Nop, Implied}, {Rra, AbsoluteY}, {Nop, AbsoluteX}, {Adc, AbsoluteX}, {Ror, AbsoluteX}, {Rra, AbsoluteX},
      // $80
      {Nop, Immediate}, {Sta, IndirectX}, {Nop, Immediate}, {Sax, IndirectX}, {Sty, ZeroPage}, {Sta, ZeroPage}, {Stx, ZeroPage}, {Sax, ZeroPage},
      {Dey, Implied}, {Nop, Immediate}, {Txa, Implied}, UNDEF, {Sty, Absolute}, {Sta, Absolute}, {Stx, Absolute}, {Sax, Absolute},
      // $90
      {Bcc, Relative}, {Sta, IndirectY}, UNDEF, UNDEF, {Sty, ZeroPageX}, {Sta, ZeroPageX}, {Stx, ZeroPageY}, {Sax, ZeroPageY},
      {Tya, Implied}, {Sta, AbsoluteY}, {Txs, Implied}, UNDEF, UNDEF, {Sta, AbsoluteX}, UNDEF, UNDEF,
      // $A0
      {Ldy, Immediate}, {Lda, IndirectX}, {Ldx, Immediate}, {Lax, IndirectX}, {Ldy, ZeroPage}, {Lda, ZeroPage}, {Ldx, ZeroPage}, {Lax, ZeroPage},
      {Tay, Implied}, {Lda, Immediate}, {Tax, Implied}, {Lax, Immediate}, {Ldy, Absolute}, {Lda, Absolute}, {Ldx, Absolute}, {Lax, Absolute},
      // $B0
      {Bcs, Relative}, {Lda, IndirectY}, UNDEF, {Lax, IndirectY}, {Ldy, ZeroPageX}, {Lda, ZeroPageX}, {Ldx, ZeroPageY}, {Lax, ZeroPageY},
      {Clv, Implied}, {Lda, AbsoluteY}, {Tsx, Implied}, UNDEF, {Ldy, AbsoluteX}, {Lda, AbsoluteX}, {Ldx, AbsoluteY}, {Lax, AbsoluteY},
      // $C0
      {Cpy, Immediate}, {Cmp, IndirectX}, {Nop, Immediate}, {Dcp, IndirectX}, {Cpy, ZeroPage}, {Cmp, ZeroPage}, {Dec, ZeroPage}, {Dcp, ZeroPage},
      {Iny, Implied}, {Cmp, Immediate}, {Dex, Implied}, UNDEF, {Cpy, Absolute}, {Cmp, Absolute}, {Dec, Absolute}, {Dcp, Absolute},
      // $D0
      {Bne, Relative}, {Cmp, IndirectY}, UNDEF, {Dcp, IndirectY}, {Nop, ZeroPageX}, {Cmp, ZeroPageX}, {Dec, ZeroPageX}, {Dcp, ZeroPageX},
      {Cld, Implied}, {Cmp, AbsoluteY}, {Nop, Implied}, {Dcp, AbsoluteY}, {Nop, AbsoluteX}, {Cmp, AbsoluteX}, {Dec, AbsoluteX}, {Dcp, AbsoluteX},
      // $E0
      {Cpx, Immediate}, {Sbc, IndirectX}, {Nop, Immediate}, {Isb, IndirectX}, {Cpx, ZeroPage}, {Sbc, ZeroPage}, {Inc, ZeroPage}, {Isb, ZeroPage},
      {Inx, Implied}, {Sbc, Immediate}, {Nop, Implied}, {Sbc, Immediate}, {Cpx, Absolute}, {Sbc, Absolute}, {Inc, Absolute}, {Isb, Absolute},
      // $F0
      {Beq, Relative}, {Sbc, IndirectY}, UNDEF, {Isb, IndirectY}, {Nop, ZeroPageX}, {Sbc, ZeroPageX}, {Inc, ZeroPageX}, {Isb, ZeroPageX},
      {Sed, Implied}, {Sbc, AbsoluteY}, {Nop, Implied}, {Isb, AbsoluteY}, {Nop, AbsoluteX}, {Sbc, AbsoluteX}, {Inc, AbsoluteX}, {Isb, AbsoluteX},
  }};
}

}  // namespace detail

/** 操作码表，下标是操作码。 */
inline constexpr std::array<OpcodeInfo, 256> OPCODES = detail::makeOpcodeTable();

}  // namespace ember::logic::hardware
