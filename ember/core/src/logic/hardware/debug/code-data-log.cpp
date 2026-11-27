/*
 * ------------------------------------------------------------------------
 * 名称：code-data-log.cpp
 * 说明：代码与数据标记的实现，见 code-data-log.hpp。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * ------------------------------------------------------------------------
 */

#include "logic/hardware/debug/code-data-log.hpp"

#include <utility>

#include "contracts/hardware/hardware-error.hpp"
#include "shared/errors/core-error.hpp"

namespace ember::logic::hardware {

namespace hw = contracts::hardware;

void CodeDataLog::start(std::size_t size, hw::CdlResolver resolver) {
  if (active_) throw shared::CoreError(hw::ERROR_CDL_ACTIVE, "代码与数据标记已经在记录");
  flags_.assign(size, 0);
  resolver_ = std::move(resolver);
  active_ = true;
}

void CodeDataLog::mark(std::uint16_t address, std::uint8_t flag) {
  const auto offset = resolver_(address);
  if (offset.has_value() && *offset < flags_.size()) flags_[*offset] |= flag;
}

void CodeDataLog::onInstruction(const hw::CpuRegisters& registers, const PeekFunction& peek) {
  const Disassembly code = disassemble(registers.pc, registers, peek, false);
  for (std::uint8_t index = 0; index < code.length; ++index) mark(static_cast<std::uint16_t>(registers.pc + index), hw::CDL_CODE);
  if (code.dataAddress.has_value()) mark(*code.dataAddress, hw::CDL_DATA);
  if (code.flow == Flow::Call) mark(code.target, hw::CDL_SUB_ENTRY);
  else if (code.flow == Flow::Jump || (code.flow == Flow::Branch && code.taken)) mark(code.target, hw::CDL_JUMP_TARGET);
}

}  // namespace ember::logic::hardware
