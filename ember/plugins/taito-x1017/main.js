/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：Taito X1017的寄存器逻辑。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-17
 * 备注：寄存器写入后统一重新计算各窗口，与状态保持一致。
 * ------------------------------------------------------------------------
 */

const CHR = ["chr-0000","chr-0400","chr-0800","chr-0c00","chr-1000","chr-1400","chr-1800","chr-1c00"];
const RAM = ["ram-6000","ram-6400","ram-6800","ram-6c00","ram-7000"];
const PERMISSION = [0xCA, 0xCA, 0x69, 0x69, 0x84];

// 没有写入对应的值时，这一段 RAM 不映射（读开放总线、写被丢弃）。
function updateRamAccess(ctx) {
  const state = ctx.state;
  const values = [state.ramPermission0, state.ramPermission0, state.ramPermission1, state.ramPermission1, state.ramPermission2];
  for (let index = 0; index < 5; index += 1) {
    if (values[index] === PERMISSION[index]) ctx.window(RAM[index]).setBank(index);
    else ctx.window(RAM[index]).unmap();
  }
}

// 两个 2K 的寄存器忽略最低位。
function updateChr(ctx) {
  const state = ctx.state;
  const regs = [state.chrReg0, state.chrReg1, state.chrReg2, state.chrReg3, state.chrReg4, state.chrReg5];
  const set = (slot, bank) => ctx.window(CHR[slot]).setBank(bank);
  const first = state.chrMode === 0 ? 0 : 4;
  const second = state.chrMode === 0 ? 4 : 0;
  set(first, regs[0] & 0xFE);
  set(first + 1, (regs[0] & 0xFE) + 1);
  set(first + 2, regs[1] & 0xFE);
  set(first + 3, (regs[1] & 0xFE) + 1);
  for (let index = 0; index < 4; index += 1) set(second + index, regs[2 + index]);
}

export default {
  reset(ctx, kind) {
    if (kind === 'power') updateRamAccess(ctx);
  },
  write(ctx, bus, addr, value) {
    const state = ctx.state;
    if (addr >= 0x7EF0 && addr <= 0x7EF5) {
      state['chrReg' + (addr & 0x0F)] = value;
      updateChr(ctx);
    } else if (addr === 0x7EF6) {
      ctx.nametable.set((value & 0x01) !== 0 ? 'vertical' : 'horizontal');
      state.chrMode = (value & 0x02) >> 1;
      updateChr(ctx);
    } else if (addr >= 0x7EF7 && addr <= 0x7EF9) {
      state['ramPermission' + ((addr & 0x0F) - 7)] = value;
      updateRamAccess(ctx);
    } else if (addr >= 0x7EFA && addr <= 0x7EFC) {
      let page = value >> 2;
      if (ctx.header.mapper !== 82) {
        // mapper 552：bank 号的 6 位顺序相反。
        page = ((value & 0x20) >> 5) | ((value & 0x10) >> 3) | ((value & 0x08) >> 1) | ((value & 0x04) << 1) | ((value & 0x02) << 3) | ((value & 0x01) << 5);
      }
      ctx.window(['prg-8000', 'prg-a000', 'prg-c000'][addr - 0x7EFA]).setBank(page);
    }
  }
};
