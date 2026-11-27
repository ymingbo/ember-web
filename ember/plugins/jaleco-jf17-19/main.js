/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：Jaleco JF-17/JF-19的寄存器逻辑。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-17
 * 备注：寄存器写入后统一重新计算各窗口，与状态保持一致。
 * ------------------------------------------------------------------------
 */

export default {
  write(ctx, bus, addr, value) {
    const state = ctx.state;
    if (state.prgFlag === 0 && (value & 0x80) !== 0) {
      if (ctx.header.mapper === 92) ctx.window('prg-c000-16k').setBank(value & 0x0F);
      else ctx.window('prg-8000-16k').setBank(value & 0x07);
    }
    if (state.chrFlag === 0 && (value & 0x40) !== 0) ctx.window('chr-0000').setBank(value & 0x0F);
    state.prgFlag = (value & 0x80) !== 0 ? 1 : 0;
    state.chrFlag = (value & 0x40) !== 0 ? 1 : 0;
  }
};
