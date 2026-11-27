/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：Mapper 202的寄存器逻辑。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-17
 * 备注：寄存器写入后统一重新计算各窗口，与状态保持一致。
 * ------------------------------------------------------------------------
 */

export default {
  write(ctx, bus, addr) {
    const bank = (addr >> 1) & 0x07;
    ctx.window('chr-0000').setBank(bank);
    ctx.window('prg-8000-16k').setBank(bank);
    ctx.window('prg-c000-16k').setBank((addr & 0x09) === 0x09 ? bank + 1 : bank);
    ctx.nametable.set((addr & 0x01) !== 0 ? 'horizontal' : 'vertical');
  }
};
