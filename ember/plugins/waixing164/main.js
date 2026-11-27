/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：Waixing 164的寄存器逻辑。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-17
 * 备注：寄存器写入后统一重新计算各窗口，与状态保持一致。
 * ------------------------------------------------------------------------
 */

export default {
  reset(ctx, kind) {
    if (kind === 'power') ctx.state.prgBank = 0x0F;
  },
  write(ctx, bus, addr, value) {
    const state = ctx.state;
    switch (addr & 0x7300) {
      case 0x5000:
        state.prgBank = (state.prgBank & 0xF0) | (value & 0x0F);
        ctx.window('prg-8000-32k').setBank(state.prgBank);
        break;
      case 0x5100:
        state.prgBank = (state.prgBank & 0x0F) | ((value & 0x0F) << 4);
        ctx.window('prg-8000-32k').setBank(state.prgBank);
        break;
    }
  }
};
