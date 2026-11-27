/*
 * ------------------------------------------------------------------------
 * 名称：cpu-disassembler.hpp
 * 说明：6502 反汇编：把一条指令写成 nestest 日志风格的文本，含有效地址与当前取值的注释。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * 备注：读内存经调用方给的无副作用读取函数；芯片寄存器类地址读出 0。非官方指令的助记符按 nestest 的写法（DCP、ISB、LAX、SAX、SLO、RLA、RRA、SRE、SBC、NOP 等），
 *       其余非官方指令用通用名称（ANC、ALR、ARR、AXS、XAA、AHX、TAS、SHX、SHY、LAS、KIL）。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

#include "contracts/hardware/cpu.hpp"

namespace ember::logic::hardware {

/** 无副作用地读 CPU 总线上的一个字节。 */
using PeekFunction = std::function<std::uint8_t(std::uint16_t)>;

/** 指令对控制流的影响。 */
enum class Flow : std::uint8_t {
  /** 顺序执行。 */
  None,
  /** JMP：跳到 target。 */
  Jump,
  /** JSR：调用 target 处的子程序。 */
  Call,
  /** 条件分支：taken 为 true 时跳到 target。 */
  Branch
};

/** 一条指令的反汇编。 */
struct Disassembly {
  /** 指令的字节数（1 到 3）。 */
  std::uint8_t length{1};
  /** 是不是非官方指令。 */
  bool unofficial{false};
  /** 助记符与操作数，带地址与取值的注释，如 "LDA ($80,X) @ 80 = 0200 = 5A"；withText 为 false 时为空。 */
  std::string text;
  Flow flow{Flow::None};
  /** Flow 不是 None 时的目标地址（JMP 间接时是指针里的值）。 */
  std::uint16_t target{0};
  /** Branch：按当前的状态寄存器，这次会不会跳转。 */
  bool taken{false};
  /** 指令读取的内存操作数的有效地址；没有内存操作数、只写（STA 等）或跳转时为空。JMP 间接给出指针的地址。 */
  std::optional<std::uint16_t> dataAddress;
};

/**
 * 反汇编 pc 处的指令。
 * @param registers 当前的寄存器，带索引的寻址用 X、Y 算有效地址，分支按状态寄存器判断是否跳转。
 * @param withText 为 false 时不生成文本（只要长度、控制流与数据地址时更快）。
 */
Disassembly disassemble(std::uint16_t pc, const contracts::hardware::CpuRegisters& registers, const PeekFunction& peek, bool withText = true);

}  // namespace ember::logic::hardware
