/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：Mapper 61的寄存器逻辑。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-17
 * 备注：寄存器写入后统一重新计算各窗口，与状态保持一致。
 * ------------------------------------------------------------------------
 */

export default {
  write(ctx, bus, addr) {
    const page = ((addr & 0x0F) << 1) | ((addr >> 5) & 0x01);
    if ((addr & 0x10) !== 0) {
      ctx.window('prg-8000-16k').setBank(page);
      ctx.window('prg-c000-16k').setBank(page);
    } else {
      ctx.window('prg-8000-16k').setBank(page & 0xFE);
      ctx.window('prg-c000-16k').setBank((page & 0xFE) + 1);
    }
    ctx.nametable.set((addr & 0x80) !== 0 ? 'horizontal' : 'vertical');
  }
};
