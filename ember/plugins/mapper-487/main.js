/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：Mapper 487的寄存器逻辑。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-17
 * 备注：寄存器写入后统一重新计算各窗口，与状态保持一致。
 * ------------------------------------------------------------------------
 */

function update(ctx) {
  const state = ctx.state;
  let prg = state.reg1 & 0x1E;
  let chr = ((state.reg1 & 0x1E) << 2) | (state.reg0 & 0x03);
  if ((state.reg1 & 0x40) !== 0) {
    // 64K 的 bank：用内层位当 A15。
    prg |= (state.reg0 & 0x08) >> 3;
    chr |= state.reg0 & 0x04;
  } else {
    // 32K 的 bank：用外层位 0 当 A15。
    prg |= state.reg1 & 0x01;
    chr |= (state.reg1 & 0x01) << 2;
  }
  if ((state.reg1 & 0x20) !== 0) {
    // 第二组 PRG/CHR 从 512K 之后开始。
    prg += 0x10;
    chr += 0x40;
  }
  ctx.window('prg-8000-32k').setBank(prg);
  ctx.window('chr-0000').setBank(chr);
  ctx.nametable.set((state.reg1 & 0x80) !== 0 ? 'horizontal' : 'vertical');
}

export default {
  reset(ctx) {
    update(ctx);
  },
  write(ctx, bus, addr, value) {
    const state = ctx.state;
    if (addr < 0x6000) {
      if ((addr & 0x100) === 0) return;
      if ((addr & 0x80) !== 0) state.reg1 = value;
      else if ((state.reg1 & 0x20) === 0) state.reg0 = value & 0x0F;
      update(ctx);
    } else if ((state.reg1 & 0x20) !== 0) {
      state.reg0 = ((value & 0x01) << 3) | ((value & 0x70) >> 4);
      update(ctx);
    }
  }
};
