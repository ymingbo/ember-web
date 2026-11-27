/*
 * ------------------------------------------------------------------------
 * 名称：cpu-6502.cpp
 * 说明：6502 CPU 核的实现。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-25
 * 备注：指令按真机的总线周期逐个执行，包括空读与读改写指令的假写；中断采样沿用“周期结束时采样、倒数第二个周期的结果生效”的模型。
 * ------------------------------------------------------------------------
 */

#include "logic/hardware/cpu-6502/cpu-6502.hpp"

#include "contracts/hardware/hardware-error.hpp"
#include "shared/errors/core-error.hpp"

namespace ember::logic::hardware {

namespace {

namespace hw = contracts::hardware;

constexpr std::uint8_t FLAG_CARRY = 0x01;
constexpr std::uint8_t FLAG_ZERO = 0x02;
constexpr std::uint8_t FLAG_INTERRUPT = 0x04;
constexpr std::uint8_t FLAG_DECIMAL = 0x08;
/** 只出现在压栈的状态副本里：软件中断（BRK、PHP）为 1，硬件中断为 0。 */
constexpr std::uint8_t FLAG_BREAK = 0x10;
/** 恒为 1 的位。 */
constexpr std::uint8_t FLAG_UNUSED = 0x20;
constexpr std::uint8_t FLAG_OVERFLOW = 0x40;
constexpr std::uint8_t FLAG_NEGATIVE = 0x80;
/** 状态寄存器里真正保存的位。 */
constexpr std::uint8_t STORED_FLAGS = FLAG_CARRY | FLAG_ZERO | FLAG_INTERRUPT | FLAG_DECIMAL | FLAG_OVERFLOW | FLAG_NEGATIVE;

constexpr std::uint16_t STACK_BASE = 0x0100;
constexpr std::uint16_t NMI_VECTOR = 0xFFFA;
constexpr std::uint16_t RESET_VECTOR = 0xFFFC;
constexpr std::uint16_t IRQ_VECTOR = 0xFFFE;

/** RDY 线连续有效超过这个周期数，认为暂停方没有释放。 */
constexpr std::uint32_t MAX_STALL_CYCLES = 1'000'000;

/** 拼出 16 位地址。 */
constexpr std::uint16_t makeAddress(std::uint8_t low, std::uint8_t high) { return static_cast<std::uint16_t>(low | (high << 8)); }

/** 栈上的地址。 */
constexpr std::uint16_t stackAddress(std::uint8_t sp) { return static_cast<std::uint16_t>(STACK_BASE | sp); }

}  // namespace

Cpu6502::Cpu6502(hw::CpuBus& bus, hw::Clock& clock, hw::ClockDomain domain, hw::IrqLine& irq, hw::IrqLine& nmi, hw::RdyLine& rdy)
    : bus_(bus), clock_(clock), domain_(domain), irq_(irq), nmi_(nmi), rdy_(rdy) {
  if (domain_.ticksPerCycle < 1) throw shared::CoreError(hw::ERROR_INVALID_ARGUMENT, "CPU 周期的主时钟周期数必须大于 0");
}

// ---------------------------------------------------------------- 对外接口

void Cpu6502::reset(bool power) {
  if (callObserver_ != nullptr) callObserver_->onResetBegin();
  if (power) {
    pc_ = 0;
    a_ = 0;
    x_ = 0;
    y_ = 0;
    sp_ = 0;
    status_ = FLAG_INTERRUPT;
    cycles_ = 0;
  }
  needNmi_ = false;
  prevNeedNmi_ = false;
  runIrq_ = false;
  prevRunIrq_ = false;
  // 复位前已经有效的 NMI 不算新的边沿。
  nmiLevel_ = nmi_.active();

  // 复位序列共 7 个周期：两次空读、三次“压栈”（只读不写，栈指针照常下移）、读复位向量。
  readCycle(pc_);
  readCycle(pc_);
  for (int i = 0; i < 3; ++i) {
    readCycle(stackAddress(sp_));
    --sp_;
  }
  setFlag(FLAG_INTERRUPT, true);
  const std::uint8_t low = readCycle(RESET_VECTOR);
  const std::uint8_t high = readCycle(RESET_VECTOR + 1);
  pc_ = makeAddress(low, high);
  if (callObserver_ != nullptr) callObserver_->onReset();
}

std::uint32_t Cpu6502::run(std::uint32_t cycles) {
  const std::uint64_t start = cycles_;
  while (cycles_ - start < cycles) {
    executeInstruction();
    // 用倒数第二个周期结束时的采样结果决定是否响应中断。
    if (prevNeedNmi_ || prevRunIrq_) interrupt(false);
  }
  return static_cast<std::uint32_t>(cycles_ - start);
}

hw::CpuRegisters Cpu6502::registers() const {
  return hw::CpuRegisters{pc_, a_, x_, y_, sp_, static_cast<std::uint8_t>(status_ | FLAG_UNUSED), cycles_};
}

void Cpu6502::setRegisters(const hw::CpuRegisters& registers) {
  pc_ = registers.pc;
  a_ = registers.a;
  x_ = registers.x;
  y_ = registers.y;
  sp_ = registers.sp;
  // 只保存 C、Z、I、D、V、N 六位。
  status_ = static_cast<std::uint8_t>(registers.status & 0xCFU);
}

// ---------------------------------------------------------------- 总线周期

std::uint8_t Cpu6502::readCycle(std::uint16_t address) {
  stallWhileHalted();
  const hw::MasterTime first = domain_.ticksPerCycle / 2;
  clock_.advance(first);
  const std::uint8_t value = bus_.read(address);
  clock_.advance(domain_.ticksPerCycle - first);
  finishCycle();
  return value;
}

void Cpu6502::writeCycle(std::uint16_t address, std::uint8_t value) {
  const hw::MasterTime first = domain_.ticksPerCycle / 2;
  clock_.advance(first);
  bus_.write(address, value);
  clock_.advance(domain_.ticksPerCycle - first);
  finishCycle();
}

void Cpu6502::stallWhileHalted() {
  std::uint32_t stalled = 0;
  while (rdy_.active()) {
    if (++stalled > MAX_STALL_CYCLES) throw shared::CoreError(hw::ERROR_CPU_STALLED, "RDY 线长时间有效，暂停方没有释放");
    // 暂停的周期不访问总线，时钟照常走，暂停方的事件在其中触发。
    clock_.advance(domain_.ticksPerCycle);
    finishCycle();
  }
}

void Cpu6502::finishCycle() {
  ++cycles_;
  prevNeedNmi_ = needNmi_;
  prevRunIrq_ = runIrq_;
  const bool nmiNow = nmi_.active();
  if (!nmiLevel_ && nmiNow) needNmi_ = true;
  nmiLevel_ = nmiNow;
  runIrq_ = irq_.active() && !flag(FLAG_INTERRUPT);
}

std::uint8_t Cpu6502::fetch() { return readCycle(pc_++); }

void Cpu6502::push(std::uint8_t value) {
  writeCycle(stackAddress(sp_), value);
  --sp_;
}

std::uint8_t Cpu6502::pull() {
  ++sp_;
  return readCycle(stackAddress(sp_));
}

// ---------------------------------------------------------------- 指令

void Cpu6502::executeInstruction() {
  const std::uint8_t opcode = fetch();
  const OpcodeInfo info = OPCODES[opcode];

  switch (info.op) {
    case Op::Brk:
      interrupt(true);
      return;
    case Op::Jsr:
      jsr();
      return;
    case Op::Rts:
      rts();
      return;
    case Op::Rti:
      rti();
      return;
    case Op::Jmp:
      jmp(info.mode);
      return;
    case Op::Pha:
      readCycle(pc_);
      push(a_);
      return;
    case Op::Php:
      readCycle(pc_);
      push(static_cast<std::uint8_t>(status_ | FLAG_BREAK | FLAG_UNUSED));
      return;
    case Op::Pla:
      readCycle(pc_);
      readCycle(stackAddress(sp_));
      a_ = pull();
      setZeroNegative(a_);
      return;
    case Op::Plp:
      readCycle(pc_);
      readCycle(stackAddress(sp_));
      status_ = static_cast<std::uint8_t>(pull() & STORED_FLAGS);
      return;
    case Op::Bpl:
      branch(!flag(FLAG_NEGATIVE));
      return;
    case Op::Bmi:
      branch(flag(FLAG_NEGATIVE));
      return;
    case Op::Bvc:
      branch(!flag(FLAG_OVERFLOW));
      return;
    case Op::Bvs:
      branch(flag(FLAG_OVERFLOW));
      return;
    case Op::Bcc:
      branch(!flag(FLAG_CARRY));
      return;
    case Op::Bcs:
      branch(flag(FLAG_CARRY));
      return;
    case Op::Bne:
      branch(!flag(FLAG_ZERO));
      return;
    case Op::Beq:
      branch(flag(FLAG_ZERO));
      return;
    default:
      break;
  }

  if (info.mode == Mode::Implied || info.mode == Mode::Accumulator) {
    readCycle(pc_);
    executeImplied(info.op);
    return;
  }

  switch (info.op) {
    case Op::Sta:
    case Op::Stx:
    case Op::Sty:
    case Op::Sax:
      writeCycle(effectiveAddress(info.mode, Access::Write), storeValue(info.op));
      return;
    case Op::Asl:
    case Op::Lsr:
    case Op::Rol:
    case Op::Ror:
    case Op::Inc:
    case Op::Dec:
    case Op::Slo:
    case Op::Rla:
    case Op::Sre:
    case Op::Rra:
    case Op::Dcp:
    case Op::Isb: {
      // 读改写：先写回原值（假写），再写新值。
      const std::uint16_t address = effectiveAddress(info.mode, Access::Modify);
      const std::uint8_t value = readCycle(address);
      writeCycle(address, value);
      writeCycle(address, modifyValue(info.op, value));
      return;
    }
    default: {
      const std::uint16_t address = effectiveAddress(info.mode, Access::Read);
      executeRead(info.op, readCycle(address));
      return;
    }
  }
}

void Cpu6502::interrupt(bool software) {
  // 硬件中断没有取指，用两次空读代替；BRK 在取指后还要读一个签名字节。
  if (software) {
    readCycle(pc_);
    ++pc_;
  } else {
    readCycle(pc_);
    readCycle(pc_);
  }
  push(static_cast<std::uint8_t>(pc_ >> 8));
  push(static_cast<std::uint8_t>(pc_ & 0xFF));
  push(static_cast<std::uint8_t>(status_ | FLAG_UNUSED | (software ? FLAG_BREAK : 0)));
  // 向量在压栈之后才选：此时到来的 NMI 会劫持 IRQ 与 BRK 的向量。
  const std::uint16_t returnAddress = pc_;
  std::uint16_t vector = IRQ_VECTOR;
  if (needNmi_) {
    needNmi_ = false;
    vector = NMI_VECTOR;
  }
  setFlag(FLAG_INTERRUPT, true);
  const std::uint8_t low = readCycle(vector);
  const std::uint8_t high = readCycle(static_cast<std::uint16_t>(vector + 1));
  pc_ = makeAddress(low, high);
  if (observer_ != nullptr) observer_->onInterrupt(software ? hw::InterruptKind::Brk : (vector == NMI_VECTOR ? hw::InterruptKind::Nmi : hw::InterruptKind::Irq), returnAddress, pc_);
}

std::uint16_t Cpu6502::effectiveAddress(Mode mode, Access access) {
  switch (mode) {
    case Mode::Immediate:
      return pc_++;
    case Mode::ZeroPage:
      return fetch();
    case Mode::ZeroPageX:
    case Mode::ZeroPageY: {
      const std::uint8_t base = fetch();
      readCycle(base);
      return static_cast<std::uint8_t>(base + (mode == Mode::ZeroPageX ? x_ : y_));
    }
    case Mode::Absolute: {
      const std::uint8_t low = fetch();
      const std::uint8_t high = fetch();
      return makeAddress(low, high);
    }
    case Mode::AbsoluteX:
    case Mode::AbsoluteY: {
      const std::uint8_t low = fetch();
      const std::uint8_t high = fetch();
      return indexedAddress(makeAddress(low, high), mode == Mode::AbsoluteX ? x_ : y_, access);
    }
    case Mode::IndirectX: {
      const std::uint8_t zeroPage = fetch();
      readCycle(zeroPage);
      const auto pointer = static_cast<std::uint8_t>(zeroPage + x_);
      const std::uint8_t low = readCycle(pointer);
      const std::uint8_t high = readCycle(static_cast<std::uint8_t>(pointer + 1));
      return makeAddress(low, high);
    }
    case Mode::IndirectY: {
      const std::uint8_t zeroPage = fetch();
      const std::uint8_t low = readCycle(zeroPage);
      const std::uint8_t high = readCycle(static_cast<std::uint8_t>(zeroPage + 1));
      return indexedAddress(makeAddress(low, high), y_, access);
    }
    default:
      // 其余寻址方式不经这里取地址。
      return 0;
  }
}

std::uint16_t Cpu6502::indexedAddress(std::uint16_t base, std::uint8_t index, Access access) {
  const auto address = static_cast<std::uint16_t>(base + index);
  const bool crossed = ((base ^ address) & 0xFF00) != 0;
  // 高字节还没修正时先读一次错误的地址：写和读改写一定有，读只在跨页时有。
  if (access != Access::Read || crossed) readCycle(static_cast<std::uint16_t>((base & 0xFF00) | (address & 0x00FF)));
  return address;
}

void Cpu6502::executeImplied(Op op) {
  switch (op) {
    case Op::Clc:
      setFlag(FLAG_CARRY, false);
      break;
    case Op::Sec:
      setFlag(FLAG_CARRY, true);
      break;
    case Op::Cli:
      setFlag(FLAG_INTERRUPT, false);
      break;
    case Op::Sei:
      setFlag(FLAG_INTERRUPT, true);
      break;
    case Op::Clv:
      setFlag(FLAG_OVERFLOW, false);
      break;
    case Op::Cld:
      setFlag(FLAG_DECIMAL, false);
      break;
    case Op::Sed:
      setFlag(FLAG_DECIMAL, true);
      break;
    case Op::Tax:
      x_ = a_;
      setZeroNegative(x_);
      break;
    case Op::Txa:
      a_ = x_;
      setZeroNegative(a_);
      break;
    case Op::Tay:
      y_ = a_;
      setZeroNegative(y_);
      break;
    case Op::Tya:
      a_ = y_;
      setZeroNegative(a_);
      break;
    case Op::Tsx:
      x_ = sp_;
      setZeroNegative(x_);
      break;
    case Op::Txs:
      sp_ = x_;
      break;
    case Op::Inx:
      setZeroNegative(++x_);
      break;
    case Op::Iny:
      setZeroNegative(++y_);
      break;
    case Op::Dex:
      setZeroNegative(--x_);
      break;
    case Op::Dey:
      setZeroNegative(--y_);
      break;
    case Op::Asl:
      a_ = shiftLeft(a_);
      break;
    case Op::Lsr:
      a_ = shiftRight(a_);
      break;
    case Op::Rol:
      a_ = rotateLeft(a_);
      break;
    case Op::Ror:
      a_ = rotateRight(a_);
      break;
    default:
      // NOP 与表里没有实现的操作码：什么也不做。
      break;
  }
}

void Cpu6502::executeRead(Op op, std::uint8_t value) {
  switch (op) {
    case Op::Lda:
      a_ = value;
      setZeroNegative(a_);
      break;
    case Op::Ldx:
      x_ = value;
      setZeroNegative(x_);
      break;
    case Op::Ldy:
      y_ = value;
      setZeroNegative(y_);
      break;
    case Op::Lax:
      a_ = value;
      x_ = value;
      setZeroNegative(value);
      break;
    case Op::And:
      a_ &= value;
      setZeroNegative(a_);
      break;
    case Op::Ora:
      a_ |= value;
      setZeroNegative(a_);
      break;
    case Op::Eor:
      a_ ^= value;
      setZeroNegative(a_);
      break;
    case Op::Adc:
      adc(value);
      break;
    case Op::Sbc:
      sbc(value);
      break;
    case Op::Cmp:
      compare(a_, value);
      break;
    case Op::Cpx:
      compare(x_, value);
      break;
    case Op::Cpy:
      compare(y_, value);
      break;
    case Op::Bit:
      setFlag(FLAG_ZERO, (a_ & value) == 0);
      setFlag(FLAG_OVERFLOW, (value & 0x40) != 0);
      setFlag(FLAG_NEGATIVE, (value & 0x80) != 0);
      break;
    default:
      // NOP 的各种变体：读了操作数，什么也不做。
      break;
  }
}

std::uint8_t Cpu6502::storeValue(Op op) const {
  switch (op) {
    case Op::Stx:
      return x_;
    case Op::Sty:
      return y_;
    case Op::Sax:
      return static_cast<std::uint8_t>(a_ & x_);
    default:
      return a_;
  }
}

std::uint8_t Cpu6502::modifyValue(Op op, std::uint8_t value) {
  switch (op) {
    case Op::Asl:
      return shiftLeft(value);
    case Op::Lsr:
      return shiftRight(value);
    case Op::Rol:
      return rotateLeft(value);
    case Op::Ror:
      return rotateRight(value);
    case Op::Inc: {
      const auto result = static_cast<std::uint8_t>(value + 1);
      setZeroNegative(result);
      return result;
    }
    case Op::Dec: {
      const auto result = static_cast<std::uint8_t>(value - 1);
      setZeroNegative(result);
      return result;
    }
    case Op::Slo: {
      const std::uint8_t result = shiftLeft(value);
      a_ |= result;
      setZeroNegative(a_);
      return result;
    }
    case Op::Rla: {
      const std::uint8_t result = rotateLeft(value);
      a_ &= result;
      setZeroNegative(a_);
      return result;
    }
    case Op::Sre: {
      const std::uint8_t result = shiftRight(value);
      a_ ^= result;
      setZeroNegative(a_);
      return result;
    }
    case Op::Rra: {
      const std::uint8_t result = rotateRight(value);
      adc(result);
      return result;
    }
    case Op::Dcp: {
      const auto result = static_cast<std::uint8_t>(value - 1);
      compare(a_, result);
      return result;
    }
    case Op::Isb: {
      const auto result = static_cast<std::uint8_t>(value + 1);
      sbc(result);
      return result;
    }
    default:
      return value;
  }
}

void Cpu6502::jsr() {
  // 操作码已经取走，pc_ 指向第一个操作数字节。
  const auto callAddress = static_cast<std::uint16_t>(pc_ - 1);
  const std::uint8_t low = fetch();
  readCycle(stackAddress(sp_));
  // 压入的是第二个操作数字节的地址，RTS 再加 1。
  push(static_cast<std::uint8_t>(pc_ >> 8));
  push(static_cast<std::uint8_t>(pc_ & 0xFF));
  const std::uint8_t high = readCycle(pc_);
  pc_ = makeAddress(low, high);
  if (callObserver_ != nullptr) callObserver_->onCall(callAddress, pc_);
}

void Cpu6502::rts() {
  readCycle(pc_);
  readCycle(stackAddress(sp_));
  const std::uint8_t low = pull();
  const std::uint8_t high = pull();
  pc_ = makeAddress(low, high);
  readCycle(pc_);
  ++pc_;
  if (callObserver_ != nullptr) callObserver_->onReturn(false);
}

void Cpu6502::rti() {
  readCycle(pc_);
  readCycle(stackAddress(sp_));
  status_ = static_cast<std::uint8_t>(pull() & STORED_FLAGS);
  const std::uint8_t low = pull();
  const std::uint8_t high = pull();
  pc_ = makeAddress(low, high);
  if (callObserver_ != nullptr) callObserver_->onReturn(true);
}

void Cpu6502::jmp(Mode mode) {
  const std::uint8_t low = fetch();
  const std::uint8_t high = fetch();
  const std::uint16_t target = makeAddress(low, high);
  if (mode == Mode::Absolute) {
    pc_ = target;
    return;
  }
  // 间接跳转的指针不跨页：指针在页尾时，高字节取自本页页首（真机的缺陷）。
  const std::uint8_t targetLow = readCycle(target);
  const std::uint8_t targetHigh = readCycle(static_cast<std::uint16_t>((target & 0xFF00) | ((target + 1) & 0x00FF)));
  pc_ = makeAddress(targetLow, targetHigh);
}

void Cpu6502::branch(bool taken) {
  const auto offset = static_cast<std::int8_t>(fetch());
  if (!taken) return;
  // 刚在这个周期才出现的 IRQ 被忽略，使成功的分支把中断推迟到下一条指令之后。
  if (runIrq_ && !prevRunIrq_) runIrq_ = false;
  readCycle(pc_);
  const auto target = static_cast<std::uint16_t>(pc_ + offset);
  // 跨页时，高字节还没修正，先在错误的地址上空读一次。
  if (((pc_ ^ target) & 0xFF00) != 0) readCycle(static_cast<std::uint16_t>((pc_ & 0xFF00) | (target & 0x00FF)));
  pc_ = target;
}

// ---------------------------------------------------------------- 标志与运算

void Cpu6502::setFlag(std::uint8_t mask, bool on) { status_ = static_cast<std::uint8_t>(on ? (status_ | mask) : (status_ & ~mask)); }

void Cpu6502::setZeroNegative(std::uint8_t value) {
  setFlag(FLAG_ZERO, value == 0);
  setFlag(FLAG_NEGATIVE, (value & 0x80) != 0);
}

void Cpu6502::addWithCarry(std::uint8_t value) {
  const unsigned sum = static_cast<unsigned>(a_) + value + (flag(FLAG_CARRY) ? 1U : 0U);
  const auto result = static_cast<std::uint8_t>(sum);
  setFlag(FLAG_CARRY, sum > 0xFF);
  setFlag(FLAG_OVERFLOW, ((a_ ^ result) & (value ^ result) & 0x80) != 0);
  a_ = result;
  setZeroNegative(a_);
}

void Cpu6502::adc(std::uint8_t value) {
  if (!decimalMode_ || !flag(FLAG_DECIMAL)) {
    addWithCarry(value);
    return;
  }
  // NMOS 6502 的 BCD 加法：Z 标志取二进制和，N 与 V 取修正高半字节之前的值，C 取修正之后的进位。
  const unsigned carryIn = flag(FLAG_CARRY) ? 1U : 0U;
  unsigned low = (a_ & 0x0FU) + (value & 0x0FU) + carryIn;
  if (low > 9U) low += 6U;
  unsigned high = (static_cast<unsigned>(a_) >> 4) + (static_cast<unsigned>(value) >> 4) + (low > 0x0FU ? 1U : 0U);
  setFlag(FLAG_ZERO, ((static_cast<unsigned>(a_) + value + carryIn) & 0xFFU) == 0);
  setFlag(FLAG_NEGATIVE, ((high << 4) & 0x80U) != 0);
  setFlag(FLAG_OVERFLOW, ((~(a_ ^ value)) & (a_ ^ (high << 4)) & 0x80U) != 0);
  if (high > 9U) high += 6U;
  setFlag(FLAG_CARRY, high > 0x0FU);
  a_ = static_cast<std::uint8_t>(((high << 4) | (low & 0x0FU)) & 0xFFU);
}

void Cpu6502::sbc(std::uint8_t value) {
  if (!decimalMode_ || !flag(FLAG_DECIMAL)) {
    addWithCarry(static_cast<std::uint8_t>(~value));
    return;
  }
  // NMOS 6502 的 BCD 减法：全部标志取二进制相减的结果，只有累加器的值按十进制修正。
  const unsigned borrow = flag(FLAG_CARRY) ? 0U : 1U;
  const unsigned difference = static_cast<unsigned>(a_) - value - borrow;
  const auto binary = static_cast<std::uint8_t>(difference);
  setFlag(FLAG_CARRY, difference < 0x100U);
  setFlag(FLAG_OVERFLOW, ((a_ ^ value) & (a_ ^ binary) & 0x80U) != 0);
  setZeroNegative(binary);
  int low = static_cast<int>(a_ & 0x0FU) - static_cast<int>(value & 0x0FU) - static_cast<int>(borrow);
  int high = static_cast<int>(a_ >> 4) - static_cast<int>(value >> 4);
  if (low < 0) {
    low -= 6;
    --high;
  }
  if (high < 0) high -= 6;
  a_ = static_cast<std::uint8_t>(((high << 4) | (low & 0x0F)) & 0xFF);
}

void Cpu6502::compare(std::uint8_t reg, std::uint8_t value) {
  setFlag(FLAG_CARRY, reg >= value);
  setZeroNegative(static_cast<std::uint8_t>(reg - value));
}

std::uint8_t Cpu6502::shiftLeft(std::uint8_t value) {
  setFlag(FLAG_CARRY, (value & 0x80) != 0);
  const auto result = static_cast<std::uint8_t>(value << 1);
  setZeroNegative(result);
  return result;
}

std::uint8_t Cpu6502::shiftRight(std::uint8_t value) {
  setFlag(FLAG_CARRY, (value & 0x01) != 0);
  const auto result = static_cast<std::uint8_t>(value >> 1);
  setZeroNegative(result);
  return result;
}

std::uint8_t Cpu6502::rotateLeft(std::uint8_t value) {
  const unsigned carryIn = flag(FLAG_CARRY) ? 1U : 0U;
  setFlag(FLAG_CARRY, (value & 0x80) != 0);
  const auto result = static_cast<std::uint8_t>((value << 1) | carryIn);
  setZeroNegative(result);
  return result;
}

std::uint8_t Cpu6502::rotateRight(std::uint8_t value) {
  const unsigned carryIn = flag(FLAG_CARRY) ? 0x80U : 0U;
  setFlag(FLAG_CARRY, (value & 0x01) != 0);
  const auto result = static_cast<std::uint8_t>((value >> 1) | carryIn);
  setZeroNegative(result);
  return result;
}

}  // namespace ember::logic::hardware
