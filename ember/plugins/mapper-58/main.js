/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：Mapper 58的寄存器逻辑。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-17
 * 备注：寄存器写入后统一重新计算各窗口，与状态保持一致。
 * ------------------------------------------------------------------------
 */

export default {
  write(ctx, bus, addr) {
    const prg = addr & 0x07;
    if ((addr & 0x40) !== 0) {
      ctx.window('prg-8000-16k').setBank(prg);
      ctx.window('prg-c000-16k').setBank(prg);
    } else {
      ctx.window('prg-8000-16k').setBank(prg & 0x06);
      ctx.window('prg-c000-16k').setBank((prg & 0x06) + 1);
    }
    ctx.window('chr-0000').setBank((addr >> 3) & 0x07);
    ctx.nametable.set((addr & 0x80) !== 0 ? 'horizontal' : 'vertical');
  }
};
