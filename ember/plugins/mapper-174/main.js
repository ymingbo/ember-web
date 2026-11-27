/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：Mapper 174的寄存器逻辑。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-17
 * 备注：寄存器写入后统一重新计算各窗口，与状态保持一致。
 * ------------------------------------------------------------------------
 */

function write(ctx, bus, addr) {
  const prg = (addr >> 4) & 0x07;
  if ((addr & 0x80) !== 0) {
    ctx.window('prg-8000-16k').setBank(prg & 0xFE);
    ctx.window('prg-c000-16k').setBank((prg & 0xFE) + 1);
  } else {
    ctx.window('prg-8000-16k').setBank(prg);
    ctx.window('prg-c000-16k').setBank(prg);
  }
  ctx.window('chr-0000').setBank((addr >> 1) & 0x07);
  ctx.nametable.set((addr & 0x01) !== 0 ? 'horizontal' : 'vertical');
}

export default {
  reset(ctx, kind) {
    if (kind === 'power') write(ctx, 'cpu', 0x8000);
  },
  write
};
