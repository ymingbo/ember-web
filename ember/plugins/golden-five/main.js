/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：Golden Five的寄存器逻辑。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-17
 * 备注：寄存器写入后统一重新计算各窗口，与状态保持一致。
 * ------------------------------------------------------------------------
 */

export default {
  write(ctx, bus, addr, value) {
    const state = ctx.state;
    if (addr >= 0xC000) {
      state.prgReg = (state.prgReg & 0xF0) | (value & 0x0F);
      ctx.window('prg-8000-16k').setBank(state.prgReg);
    } else if (addr <= 0x9FFF && (value & 0x08) !== 0) {
      state.prgReg = (state.prgReg & 0x0F) | ((value << 4) & 0x70);
      ctx.window('prg-8000-16k').setBank(state.prgReg);
      ctx.window('prg-c000-16k').setBank(((value << 4) & 0x70) | 0x0F);
    }
  }
};
