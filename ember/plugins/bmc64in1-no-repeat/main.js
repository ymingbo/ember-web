/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：BMC-64in1NoRepeat的寄存器逻辑。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-17
 * 备注：寄存器写入后统一重新计算各窗口，与状态保持一致。
 * ------------------------------------------------------------------------
 */

function update(ctx) {
  const state = ctx.state;
  const low = ctx.window('prg-8000-16k');
  const high = ctx.window('prg-c000-16k');
  const bank = ((state.reg1 & 0x1F) << 1) | ((state.reg1 >> 6) & 0x01);
  if ((state.reg0 & 0x80) !== 0) {
    if ((state.reg1 & 0x80) !== 0) {
      low.setBank((state.reg1 & 0x1F) << 1);
      high.setBank(((state.reg1 & 0x1F) << 1) + 1);
    } else {
      low.setBank(bank);
      high.setBank(bank);
    }
  } else {
    high.setBank(bank);
  }
  ctx.nametable.set((state.reg0 & 0x20) !== 0 ? 'horizontal' : 'vertical');
  ctx.window('chr-0000').setBank((state.reg2 << 2) | ((state.reg0 >> 1) & 0x03));
}

export default {
  reset(ctx) {
    ctx.state.reg0 = 0x80;
    ctx.state.reg1 = 0x43;
    ctx.state.reg2 = 0;
    ctx.state.reg3 = 0;
    update(ctx);
  },
  write(ctx, bus, addr, value) {
    if (addr < 0x8000) ctx.state['reg' + (addr & 0x03)] = value;
    else ctx.state.reg3 = value;
    update(ctx);
  }
};
