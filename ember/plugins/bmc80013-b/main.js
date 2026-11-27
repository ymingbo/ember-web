/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：BMC-80013-B的寄存器逻辑。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-17
 * 备注：寄存器写入后统一重新计算各窗口，与状态保持一致。
 * ------------------------------------------------------------------------
 */

function update(ctx) {
  const state = ctx.state;
  ctx.window('prg-8000-16k').setBank((state.mode & 0x02) !== 0 ? (state.reg0 & 0x0F) | (state.reg1 & 0x70) : state.reg0 & 0x03);
  ctx.window('prg-c000-16k').setBank(state.reg1 & 0x7F);
  ctx.nametable.set((state.reg0 & 0x10) !== 0 ? 'vertical' : 'horizontal');
}

export default {
  reset(ctx) {
    ctx.state.reg0 = 0;
    ctx.state.reg1 = 0;
    ctx.state.mode = 0;
    update(ctx);
  },
  write(ctx, bus, addr, value) {
    const reg = (addr >> 13) & 0x03;
    if (reg === 0) {
      ctx.state.reg0 = value;
    } else {
      ctx.state.reg1 = value;
      ctx.state.mode = reg;
    }
    update(ctx);
  }
};
