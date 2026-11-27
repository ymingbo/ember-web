/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：Mapper 212的寄存器逻辑。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-17
 * 备注：寄存器写入后统一重新计算各窗口，与状态保持一致。
 * ------------------------------------------------------------------------
 */

function write(ctx, bus, addr) {
  if ((addr & 0x4000) !== 0) {
    ctx.window('prg-8000-16k').setBank(addr & 0x06);
    ctx.window('prg-c000-16k').setBank((addr & 0x06) + 1);
  } else {
    ctx.window('prg-8000-16k').setBank(addr & 0x07);
    ctx.window('prg-c000-16k').setBank(addr & 0x07);
  }
  ctx.window('chr-0000').setBank(addr & 0x07);
  ctx.nametable.set((addr & 0x08) !== 0 ? 'horizontal' : 'vertical');
}

export default {
  reset(ctx, kind) {
    if (kind === 'power') write(ctx, 'cpu', 0x8000);
  },
  read(ctx, bus, addr) {
    // 先读到寄存器下面的 PRG RAM，再按地址把位 7 置 1。
    const value = ctx.peek(addr) ?? 0;
    return (addr & 0xE010) === 0x6000 ? value | 0x80 : value;
  },
  write
};
