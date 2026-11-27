/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：Mapper 57的寄存器逻辑。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-17
 * 备注：寄存器写入后统一重新计算各窗口，与状态保持一致。
 * ------------------------------------------------------------------------
 */

function update(ctx) {
  const state = ctx.state;
  ctx.nametable.set((state.reg1 & 0x08) !== 0 ? 'horizontal' : 'vertical');
  ctx.window('chr-0000').setBank(((state.reg0 & 0x40) >> 3) | ((state.reg0 | state.reg1) & 0x07));
  const prg = (state.reg1 >> 5) & 0x07;
  if ((state.reg1 & 0x10) !== 0) {
    ctx.window('prg-8000-16k').setBank(prg & 0x06);
    ctx.window('prg-c000-16k').setBank((prg & 0x06) + 1);
  } else {
    ctx.window('prg-8000-16k').setBank(prg);
    ctx.window('prg-c000-16k').setBank(prg);
  }
}

export default {
  reset(ctx, kind) {
    if (kind !== 'power') return;
    ctx.state.reg0 = 0;
    ctx.state.reg1 = 0;
    update(ctx);
  },
  write(ctx, bus, addr, value) {
    switch (addr & 0x8800) {
      case 0x8000:
        ctx.state.reg0 = value;
        break;
      case 0x8800:
        ctx.state.reg1 = value;
        break;
    }
    update(ctx);
  }
};
