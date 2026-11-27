/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：Caltron 41的寄存器逻辑。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-17
 * 备注：寄存器写入后统一重新计算各窗口，与状态保持一致。
 * ------------------------------------------------------------------------
 */

// 地址在 $6000-$67FF 时写外层，否则写 CHR 内层（PRG bank 为 4-7 时才允许）。
function write(ctx, bus, addr, value) {
  const state = ctx.state;
  if (addr <= 0x67FF) {
    state.prgBank = addr & 0x07;
    state.chrBank = (state.chrBank & 0x03) | ((addr >> 1) & 0x0C);
    ctx.window('prg-8000-32k').setBank(state.prgBank);
    ctx.window('chr-0000').setBank(state.chrBank);
    ctx.nametable.set((addr & 0x20) !== 0 ? 'horizontal' : 'vertical');
  } else if (state.prgBank >= 4) {
    state.chrBank = (state.chrBank & 0x0C) | (value & 0x03);
    ctx.window('chr-0000').setBank(state.chrBank);
  }
}

export default {
  reset(ctx) {
    ctx.state.chrBank = 0;
    ctx.state.prgBank = 0;
    write(ctx, 'cpu', 0x6000, 0);
    write(ctx, 'cpu', 0x8000, 0);
  },
  write
};
