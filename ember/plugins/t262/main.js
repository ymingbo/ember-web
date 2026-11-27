/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：T-262的寄存器逻辑。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-17
 * 备注：寄存器写入后统一重新计算各窗口，与状态保持一致。
 * ------------------------------------------------------------------------
 */

export default {
  write(ctx, bus, addr, value) {
    const state = ctx.state;
    if (state.locked === 0) {
      state.base = ((addr & 0x60) >> 2) | ((addr & 0x100) >> 3);
      state.mode = (addr & 0x80) !== 0 ? 1 : 0;
      state.locked = (addr & 0x2000) !== 0 ? 1 : 0;
      ctx.nametable.set((addr & 0x02) !== 0 ? 'horizontal' : 'vertical');
    }
    state.bank = value & 0x07;
    ctx.window('prg-8000-16k').setBank(state.base | state.bank);
    ctx.window('prg-c000-16k').setBank(state.base | (state.mode !== 0 ? state.bank : 7));
  }
};
