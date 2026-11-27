/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：Waixing 178的寄存器逻辑。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-17
 * 备注：寄存器写入后统一重新计算各窗口，与状态保持一致。
 * ------------------------------------------------------------------------
 */

function update(ctx) {
  const state = ctx.state;
  const small = state.reg1 & 0x07;
  const big = state.reg2;
  const low = ctx.window('prg-8000-16k');
  const high = ctx.window('prg-c000-16k');
  if ((state.reg0 & 0x02) !== 0) {
    low.setBank((big << 3) | small);
    high.setBank((state.reg0 & 0x04) !== 0 ? (big << 3) | 0x06 | (state.reg1 & 0x01) : (big << 3) | 0x07);
  } else {
    const bank = (big << 3) | small;
    if ((state.reg0 & 0x04) !== 0) {
      low.setBank(bank);
      high.setBank(bank);
    } else {
      low.setBank(bank);
      high.setBank(bank + 1);
    }
  }
  ctx.window('prg-ram').setBank(state.reg3 & 0x03);
  ctx.nametable.set((state.reg0 & 0x01) !== 0 ? 'horizontal' : 'vertical');
}

export default {
  reset(ctx, kind) {
    if (kind === 'power') update(ctx);
  },
  write(ctx, bus, addr, value) {
    ctx.state['reg' + (addr & 0x03)] = value;
    update(ctx);
  }
};
