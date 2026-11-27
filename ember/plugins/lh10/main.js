/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：LH10的寄存器逻辑。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-17
 * 备注：寄存器写入后统一重新计算各窗口，与状态保持一致。
 * ------------------------------------------------------------------------
 */

export default {
  write(ctx, bus, addr, value) {
    const state = ctx.state;
    switch (addr & 0xE001) {
      case 0x8000:
        state.currentRegister = value & 0x07;
        break;
      case 0x8001:
        state['reg' + state.currentRegister] = value;
        ctx.window('prg-8000').setBank(state.reg6);
        ctx.window('prg-a000').setBank(state.reg7);
        break;
    }
  }
};
