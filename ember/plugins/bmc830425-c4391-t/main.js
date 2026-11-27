/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：BMC-830425C-4391T的寄存器逻辑。
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

export default {
  write(ctx, bus, addr, value) {
    const state = ctx.state;
    state.innerReg = value & 0x0F;
    if ((addr & 0xFFE0) === 0xF0E0) {
      state.outerReg = addr & 0x0F;
      state.prgMode = (addr >> 4) & 0x01;
    }
    const outer = state.outerReg << 3;
    if (state.prgMode !== 0) set16(ctx, (state.innerReg & 0x07) | outer, 0x07 | outer);
    else set16(ctx, state.innerReg | outer, 0x0F | outer);
  }
};
