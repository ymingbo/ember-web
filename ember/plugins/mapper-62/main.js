/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：Mapper 62的寄存器逻辑。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-17
 * 备注：寄存器写入后统一重新计算各窗口，与状态保持一致。
 * ------------------------------------------------------------------------
 */

export default {
  reset(ctx, kind) {
    if (kind !== 'soft') return;
    ctx.window('prg-8000-16k').setBank(0);
    ctx.window('prg-c000-16k').setBank(1);
    ctx.window('chr-0000').setBank(0);
  },
  write(ctx, bus, addr, value) {
    const page = ((addr & 0x3F00) >> 8) | (addr & 0x40);
    if ((addr & 0x20) !== 0) {
      ctx.window('prg-8000-16k').setBank(page);
      ctx.window('prg-c000-16k').setBank(page);
    } else {
      ctx.window('prg-8000-16k').setBank(page & 0xFE);
      ctx.window('prg-c000-16k').setBank((page & 0xFE) + 1);
    }
    ctx.window('chr-0000').setBank(((addr & 0x1F) << 2) | (value & 0x03));
    ctx.nametable.set((addr & 0x80) !== 0 ? 'horizontal' : 'vertical');
  }
};
