/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：Farid UNROM的寄存器逻辑。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-17
 * 备注：寄存器写入后统一重新计算各窗口，与状态保持一致。
 * ------------------------------------------------------------------------
 */

export default {
  reset(ctx, kind) {
    // 软复位只清除外层位与锁定位，窗口保持不变。
    if (kind === 'soft') ctx.state.reg = ctx.state.reg & 0x87;
  },
  write(ctx, bus, addr, value) {
    const state = ctx.state;
    const locked = (state.reg & 0x08) !== 0;
    if (!locked && (state.reg & 0x80) === 0 && (value & 0x80) !== 0) state.reg = (state.reg & 0x87) | (value & 0x78);
    state.reg = (state.reg & 0x78) | (value & 0x87);
    const outer = state.reg & 0x70;
    ctx.window('prg-8000-16k').setBank((state.reg & 0x07) | (outer >> 1));
    ctx.window('prg-c000-16k').setBank(0x07 | (outer >> 1));
  }
};
