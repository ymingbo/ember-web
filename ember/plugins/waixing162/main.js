/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：Waixing 162的寄存器逻辑。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-17
 * 备注：寄存器写入后统一重新计算各窗口，与状态保持一致。
 * ------------------------------------------------------------------------
 */

function update(ctx) {
  const state = ctx.state;
  const high = (state.reg2 & 0x0F) << 4;
  let bank = 0;
  switch (state.reg3 & 0x05) {
    case 0:
      bank = (state.reg0 & 0x0C) | (state.reg1 & 0x02) | high;
      break;
    case 1:
      bank = (state.reg0 & 0x0C) | high;
      break;
    case 4:
      bank = (state.reg0 & 0x0E) | ((state.reg1 >> 1) & 0x01) | high;
      break;
    case 5:
      bank = (state.reg0 & 0x0F) | high;
      break;
  }
  ctx.window('prg-8000-32k').setBank(bank);
}

export default {
  reset(ctx, kind) {
    if (kind !== 'power') return;
    ctx.state.reg0 = 3;
    ctx.state.reg1 = 0;
    ctx.state.reg2 = 0;
    ctx.state.reg3 = 7;
    update(ctx);
  },
  write(ctx, bus, addr, value) {
    ctx.state['reg' + ((addr >> 8) & 0x03)] = value;
    update(ctx);
  }
};
