/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：Mapper 229的寄存器逻辑。
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
  ctx.window('chr-0000').setBank(addr & 0xFF);
  if ((addr & 0x1E) === 0) set16(ctx, 0, 1);
  else set16(ctx, addr & 0x1F, addr & 0x1F);
  ctx.nametable.set((addr & 0x20) !== 0 ? 'horizontal' : 'vertical');
}

export default {
  reset(ctx, kind) {
    if (kind === 'power') write(ctx, 'cpu', 0x8000);
  },
  write
};
