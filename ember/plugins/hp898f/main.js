/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：HP898F的寄存器逻辑。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-17
 * 备注：寄存器写入后统一重新计算各窗口，与状态保持一致。
 * ------------------------------------------------------------------------
 */

function update(ctx) {
  const state = ctx.state;
  const prg = (state.reg1 >> 3) & 7;
  const mask = (state.reg1 >> 4) & 4;
  ctx.window('chr-0000').setBank(((state.reg0 >> 4) & 0x07) & ~(((state.reg0 & 0x01) << 2) | (state.reg0 & 0x02)));
  ctx.window('prg-8000-16k').setBank(prg & ~mask);
  ctx.window('prg-c000-16k').setBank(prg | mask);
  ctx.nametable.set((state.reg1 & 0x80) !== 0 ? 'vertical' : 'horizontal');
}

export default {
  reset(ctx, kind) {
    if (kind === 'power') update(ctx);
  },
  write(ctx, bus, addr, value) {
    if ((addr & 0x6000) !== 0x6000) return;
    ctx.state['reg' + ((addr & 0x04) >> 2)] = value;
    update(ctx);
  }
};
