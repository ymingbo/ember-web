/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：NINA-003/006的寄存器逻辑。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-17
 * 备注：寄存器写入后统一重新计算各窗口，与状态保持一致。
 * ------------------------------------------------------------------------
 */

export default {
  write(ctx, bus, addr, value) {
    if ((addr & 0xE100) !== 0x4100) return;
    if (ctx.header.mapper === 113) {
      ctx.window('prg-8000-32k').setBank((value >> 3) & 0x07);
      ctx.window('chr-0000').setBank((value & 0x07) | ((value >> 3) & 0x08));
      ctx.nametable.set((value & 0x80) !== 0 ? 'vertical' : 'horizontal');
    } else {
      ctx.window('prg-8000-32k').setBank((value >> 3) & 0x01);
      ctx.window('chr-0000').setBank(value & 0x07);
    }
  }
};
