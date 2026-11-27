/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：Mapper 225的寄存器逻辑。
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
  write(ctx, bus, addr) {
    const high = (addr >> 8) & 0x40;
    const page = ((addr >> 6) & 0x3F) | high;
    if ((addr & 0x1000) !== 0) set16(ctx, page, page);
    else set16(ctx, page & 0xFE, (page & 0xFE) + 1);
    ctx.window('chr-0000').setBank((addr & 0x3F) | high);
    ctx.nametable.set((addr & 0x2000) !== 0 ? 'horizontal' : 'vertical');
  }
};
