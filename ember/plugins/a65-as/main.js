/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：A65AS的寄存器逻辑。
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
  if ((value & 0x40) !== 0) {
    set16(ctx, value & 0x1E, (value & 0x1E) + 1);
  } else {
    set16(ctx, ((value & 0x30) >> 1) | (value & 0x07), ((value & 0x30) >> 1) | 0x07);
  }
  if ((value & 0x80) !== 0) ctx.nametable.set((value & 0x20) !== 0 ? 'single1' : 'single0');
  else ctx.nametable.set((value & 0x08) !== 0 ? 'horizontal' : 'vertical');
}

export default {
  reset(ctx, kind) {
    if (kind === 'power') write(ctx, 'cpu', 0x8000, 0);
  },
  write
};
