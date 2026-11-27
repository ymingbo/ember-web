/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：Mapper 200的寄存器逻辑。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-17
 * 备注：寄存器写入后统一重新计算各窗口，与状态保持一致。
 * ------------------------------------------------------------------------
 */

export default {
  write(ctx, bus, addr) {
    const bank = addr & 0x07;
    ctx.window('prg-8000-16k').setBank(bank);
    ctx.window('prg-c000-16k').setBank(bank);
    ctx.window('chr-0000').setBank(bank);
    ctx.nametable.set((addr & 0x08) !== 0 ? 'vertical' : 'horizontal');
  }
};
