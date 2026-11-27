/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：BMC-G-146的寄存器逻辑。
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
  if ((addr & 0x800) !== 0) {
    set16(ctx, (addr & 0x1F) | (addr & ((addr & 0x40) >> 6)), (addr & 0x18) | 0x07);
  } else if ((addr & 0x40) !== 0) {
    set16(ctx, addr & 0x1F, addr & 0x1F);
  } else {
    set16(ctx, addr & 0x1E, (addr & 0x1E) + 1);
  }
  ctx.nametable.set((addr & 0x80) !== 0 ? 'horizontal' : 'vertical');
}

export default {
  reset(ctx) {
    write(ctx, 'cpu', 0x8000);
    ctx.window('chr-0000').setBank(0);
  },
  write
};
