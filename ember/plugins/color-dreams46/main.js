/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：Color Dreams（mapper 46）的寄存器逻辑。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-17
 * 备注：寄存器写入后统一重新计算各窗口，与状态保持一致。
 * ------------------------------------------------------------------------
 */

function update(ctx) {
  const state = ctx.state;
  ctx.window('prg-8000-32k').setBank(((state.reg0 & 0x0F) << 1) | (state.reg1 & 0x01));
  ctx.window('chr-0000').setBank(((state.reg0 & 0xF0) >> 1) | ((state.reg1 & 0x70) >> 4));
}

export default {
  reset(ctx) {
    ctx.state.reg0 = 0;
    ctx.state.reg1 = 0;
    update(ctx);
  },
  write(ctx, bus, addr, value) {
    if (addr < 0x8000) ctx.state.reg0 = value;
    else ctx.state.reg1 = value;
    update(ctx);
  }
};
