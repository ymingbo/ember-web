/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：Mapper 227的寄存器逻辑。
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
  const bank = ((addr >> 2) & 0x1F) | ((addr & 0x100) >> 3);
  const sFlag = (addr & 0x01) !== 0;
  const lFlag = ((addr >> 9) & 0x01) !== 0;
  const prgMode = ((addr >> 7) & 0x01) !== 0;
  if (prgMode) {
    if (sFlag) set16(ctx, bank & 0xFE, (bank & 0xFE) + 1);
    else set16(ctx, bank, bank);
  } else if (sFlag) {
    set16(ctx, bank & 0x3E, lFlag ? bank | 0x07 : bank & 0x38);
  } else {
    set16(ctx, bank, lFlag ? bank | 0x07 : bank & 0x38);
  }
  ctx.nametable.set((addr & 0x02) !== 0 ? 'horizontal' : 'vertical');
}

export default {
  reset(ctx, kind) {
    if (kind === 'power') write(ctx, 'cpu', 0x8000);
  },
  write
};
