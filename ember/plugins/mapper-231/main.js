/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：Mapper 231的寄存器逻辑。
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

export default {
  reset(ctx) {
    set16(ctx, 0, 0);
  },
  write(ctx, bus, addr) {
    const bank = ((addr >> 5) & 0x01) | (addr & 0x1E);
    set16(ctx, bank & 0x1E, bank);
    ctx.nametable.set((addr & 0x80) !== 0 ? 'horizontal' : 'vertical');
  }
};
