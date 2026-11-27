/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：Jaleco JF-16的寄存器逻辑。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-17
 * 备注：寄存器写入后统一重新计算各窗口，与状态保持一致。
 * ------------------------------------------------------------------------
 */

export default {
  write(ctx, bus, addr, value) {
    ctx.window('prg-8000-16k').setBank(value & 0x07);
    ctx.window('chr-0000').setBank((value >> 4) & 0x0F);
    if (ctx.header.variant === 3) ctx.nametable.set((value & 0x08) !== 0 ? 'vertical' : 'horizontal');
    else ctx.nametable.set((value & 0x08) !== 0 ? 'single1' : 'single0');
  }
};
