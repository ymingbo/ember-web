/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：Super 40-in-1 (WS)的寄存器逻辑。
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

function write(ctx, bus, addr, value) {
  if (ctx.state.regLock !== 0) return;
  if ((addr & 0x01) !== 0) {
    ctx.window('chr-0000').setBank(value);
    return;
  }
  ctx.state.regLock = (value & 0x20) !== 0 ? 1 : 0;
  const flag = (~value >> 3) & 0x01;
  set16(ctx, value & ~flag, value | flag);
  ctx.nametable.set((value & 0x10) !== 0 ? 'horizontal' : 'vertical');
}

export default {
  reset(ctx, kind) {
    if (kind === 'power') write(ctx, 'cpu', 0x6000, 0);
  },
  write
};
