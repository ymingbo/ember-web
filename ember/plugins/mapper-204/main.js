/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：Mapper 204的寄存器逻辑。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-17
 * 备注：寄存器写入后统一重新计算各窗口，与状态保持一致。
 * ------------------------------------------------------------------------
 */

function write(ctx, bus, addr) {
  const group = addr & 0x06;
  const page = group + (group === 0x06 ? 0 : (addr & 0x01));
  ctx.window('prg-8000-16k').setBank(page);
  ctx.window('prg-c000-16k').setBank(group + (group === 0x06 ? 1 : (addr & 0x01)));
  ctx.window('chr-0000').setBank(page);
  ctx.nametable.set((addr & 0x10) !== 0 ? 'horizontal' : 'vertical');
}

export default {
  reset(ctx, kind) {
    if (kind === 'power') write(ctx, 'cpu', 0x8000);
  },
  write
};
