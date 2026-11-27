/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：BMC-255的寄存器逻辑。
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
  const prgBit = (addr & 0x1000) !== 0 ? 0 : 1;
  const bank = ((addr >> 8) & 0x40) | ((addr >> 6) & 0x3F);
  set16(ctx, bank & ~prgBit, bank | prgBit);
  ctx.window('chr-0000').setBank(((addr >> 8) & 0x40) | (addr & 0x3F));
  ctx.nametable.set((addr & 0x2000) !== 0 ? 'horizontal' : 'vertical');
}

export default {
  reset(ctx, kind) {
    if (kind === 'power') write(ctx, 'cpu', 0x8000);
  },
  write
};
