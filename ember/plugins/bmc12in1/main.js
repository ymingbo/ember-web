/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：BMC-12in1的寄存器逻辑。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-17
 * 备注：寄存器写入后统一重新计算各窗口，与状态保持一致。
 * ------------------------------------------------------------------------
 */

const set16 = (ctx, low, high) => {
  ctx.window('prg-8000-16k').setBank(low);
  ctx.window('prg-c000-16k').setBank(high);
};

function update(ctx) {
  const state = ctx.state;
  const bank = (state.mode & 0x03) << 3;
  ctx.window('chr-0000').setBank((state.reg0 >> 3) | (bank << 2));
  ctx.window('chr-1000').setBank((state.reg1 >> 3) | (bank << 2));
  if ((state.mode & 0x08) !== 0) set16(ctx, bank | (state.reg0 & 0x06), (bank | (state.reg0 & 0x06)) + 1);
  else set16(ctx, bank | (state.reg0 & 0x07), bank | 0x07);
  ctx.nametable.set((state.mode & 0x04) !== 0 ? 'horizontal' : 'vertical');
}

export default {
  reset(ctx, kind) {
    if (kind === 'power') update(ctx);
  },
  write(ctx, bus, addr, value) {
    const state = ctx.state;
    switch (addr & 0xE000) {
      case 0xA000:
        state.reg0 = value;
        update(ctx);
        break;
      case 0xC000:
        state.reg1 = value;
        update(ctx);
        break;
      case 0xE000:
        state.mode = value & 0x0F;
        update(ctx);
        break;
    }
  }
};
