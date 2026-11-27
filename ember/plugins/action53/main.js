/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：Action 53的寄存器逻辑。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-17
 * 备注：寄存器写入后统一重新计算各窗口，与状态保持一致。
 * ------------------------------------------------------------------------
 */

const OUTER_AND = [0x1FE, 0x1FC, 0x1F8, 0x1F0];
const INNER_AND = [0x01, 0x03, 0x07, 0x0F];
const MIRRORING = ['single0', 'single1', 'vertical', 'horizontal'];

// 按寄存器重新计算名称表与 PRG、CHR 窗口。
function update(ctx) {
  const state = ctx.state;
  let mirroring = state.reg2 & 0x03;
  if ((mirroring & 0x02) === 0) mirroring = state.mirroringBit;
  ctx.nametable.set(MIRRORING[mirroring]);

  const gameSize = (state.reg2 & 0x30) >> 4;
  const prgSize = (state.reg2 & 0x08) >> 3;
  const slotSelect = (state.reg2 & 0x04) >> 2;
  const prgSelect = state.reg1 & 0x0F;
  const outer = state.reg3 << 1;
  const slots = [ctx.window('prg-8000-16k'), ctx.window('prg-c000-16k')];
  ctx.window('chr-0000').setBank(state.reg0 & 0x03);
  if (prgSize !== 0) {
    slots[slotSelect !== 0 ? 0 : 1].setBank((outer & OUTER_AND[gameSize]) | (prgSelect & INNER_AND[gameSize]));
    slots[slotSelect !== 0 ? 1 : 0].setBank((outer & 0x1FE) | slotSelect);
  } else {
    const select = (prgSelect << 1) & 0xFF;
    slots[0].setBank((outer & OUTER_AND[gameSize]) | (select & INNER_AND[gameSize]));
    slots[1].setBank((outer & OUTER_AND[gameSize]) | ((select | 0x01) & INNER_AND[gameSize]));
  }
}

export default {
  write(ctx, bus, addr, value) {
    const state = ctx.state;
    if (addr <= 0x5FFF) {
      state.selectedReg = ((value & 0x80) >> 6) | (value & 0x01);
      return;
    }
    if (state.selectedReg <= 1) state.mirroringBit = (value >> 4) & 0x01;
    else if (state.selectedReg === 2) state.mirroringBit = value & 0x01;
    state['reg' + state.selectedReg] = value;
    update(ctx);
  }
};
