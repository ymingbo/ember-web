/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：BMC-11160的寄存器逻辑。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-17
 * 备注：寄存器写入后统一重新计算各窗口，与状态保持一致。
 * ------------------------------------------------------------------------
 */

function write(ctx, bus, addr, value) {
  const bank = (value >> 4) & 0x07;
  ctx.window('prg-8000-32k').setBank(bank);
  ctx.window('chr-0000').setBank((bank << 2) | (value & 0x03));
  ctx.nametable.set((value & 0x80) !== 0 ? 'vertical' : 'horizontal');
}

export default {
  reset(ctx) {
    write(ctx, 'cpu', 0x8000, 0);
  },
  write
};
