/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：BMC-51的寄存器逻辑。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-17
 * 备注：寄存器写入后统一重新计算各窗口，与状态保持一致。
 * ------------------------------------------------------------------------
 */

// 按 bank 与模式摆放五个 8K 的 PRG 窗口（含 $6000-$7FFF 的 ROM）并设置名称表。
function update(ctx) {
  const state = ctx.state;
  const outer = state.bank << 2;
  const slots = ['prg-8000', 'prg-a000', 'prg-c000', 'prg-e000'].map((id) => ctx.window(id));
  if ((state.mode & 0x01) !== 0) {
    for (let index = 0; index < 4; index += 1) slots[index].setBank(outer + index);
    ctx.window('prg-6000').setBank(0x23 | outer);
  } else {
    slots[0].setBank(outer | state.mode);
    slots[1].setBank((outer | state.mode) + 1);
    slots[2].setBank(outer | 0x0E);
    slots[3].setBank((outer | 0x0E) + 1);
    ctx.window('prg-6000').setBank(0x2F | outer);
  }
  ctx.nametable.set(state.mode === 0x03 ? 'horizontal' : 'vertical');
}

export default {
  reset(ctx, kind) {
    if (kind !== 'power') return;
    ctx.state.bank = 0;
    ctx.state.mode = 1;
    update(ctx);
  },
  write(ctx, bus, addr, value) {
    const state = ctx.state;
    if (addr <= 0x7FFF) {
      state.mode = ((value >> 3) & 0x02) | ((value >> 1) & 0x01);
    } else if (addr >= 0xC000 && addr <= 0xDFFF) {
      state.bank = value & 0x0F;
      state.mode = ((value >> 3) & 0x02) | (state.mode & 0x01);
    } else {
      state.bank = value & 0x0F;
    }
    update(ctx);
  }
};
