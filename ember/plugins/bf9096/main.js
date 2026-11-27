/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：Codemasters BF9096的寄存器逻辑。
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
    if (addr >= 0xC000) {
      state.prgPage = value & 0x03;
    } else if (ctx.header.variant === 1) {
      // Aladdin Deck Enhancer：外层 bank 的两位顺序交换。
      state.prgBlock = ((value >> 4) & 0x01) | ((value >> 2) & 0x02);
    } else {
      state.prgBlock = (value >> 3) & 0x03;
    }
    set16(ctx, (state.prgBlock << 2) | state.prgPage, (state.prgBlock << 2) | 3);
  }
};
