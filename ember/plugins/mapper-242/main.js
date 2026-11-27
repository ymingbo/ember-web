/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：Mapper 242的寄存器逻辑。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-17
 * 备注：寄存器写入后统一重新计算各窗口，与状态保持一致。
 * ------------------------------------------------------------------------
 */

export default {
  reset(ctx) {
    ctx.window('prg-8000-32k').setBank(0);
    ctx.nametable.set('vertical');
  },
  write(ctx, bus, addr) {
    ctx.nametable.set((addr & 0x02) !== 0 ? 'horizontal' : 'vertical');
    ctx.window('prg-8000-32k').setBank((addr >> 3) & 0x0F);
  }
};
