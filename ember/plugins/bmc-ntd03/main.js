/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：BMC-NTD-03的寄存器逻辑。
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

function write(ctx, bus, addr) {
  const prg = (addr >> 10) & 0x1E;
  if ((addr & 0x80) !== 0) set16(ctx, prg | ((addr >> 6) & 1), prg | ((addr >> 6) & 1));
  else set16(ctx, prg & 0xFE, (prg & 0xFE) + 1);
  ctx.window('chr-0000').setBank(((addr & 0x0300) >> 5) | (addr & 0x07));
  ctx.nametable.set((addr & 0x400) !== 0 ? 'horizontal' : 'vertical');
}

export default {
  reset(ctx) {
    write(ctx, 'cpu', 0x8000);
  },
  write
};
