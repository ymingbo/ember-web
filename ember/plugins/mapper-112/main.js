/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：Mapper 112的寄存器逻辑。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-17
 * 备注：寄存器写入后统一重新计算各窗口，与状态保持一致。
 * ------------------------------------------------------------------------
 */

const CHR = ["chr-0000","chr-0400","chr-0800","chr-0c00","chr-1000","chr-1400","chr-1800","chr-1c00"];

function update(ctx) {
  const state = ctx.state;
  const outer = state.outerChrBank;
  ctx.window('prg-8000').setBank(state.reg0);
  ctx.window('prg-a000').setBank(state.reg1);
  ctx.window(CHR[0]).setBank(state.reg2);
  ctx.window(CHR[1]).setBank(state.reg2 + 1);
  ctx.window(CHR[2]).setBank(state.reg3);
  ctx.window(CHR[3]).setBank(state.reg3 + 1);
  ctx.window(CHR[4]).setBank(state.reg4 | ((outer & 0x10) << 4));
  ctx.window(CHR[5]).setBank(state.reg5 | ((outer & 0x20) << 3));
  ctx.window(CHR[6]).setBank(state.reg6 | ((outer & 0x40) << 2));
  ctx.window(CHR[7]).setBank(state.reg7 | ((outer & 0x80) << 1));
}

export default {
  reset(ctx, kind) {
    if (kind === 'power') update(ctx);
  },
  write(ctx, bus, addr, value) {
    const state = ctx.state;
    switch (addr & 0xE001) {
      case 0x8000:
        state.currentReg = value & 0x07;
        break;
      case 0xA000:
        state['reg' + state.currentReg] = value;
        break;
      case 0xC000:
        state.outerChrBank = value;
        break;
      case 0xE000:
        ctx.nametable.set((value & 0x01) !== 0 ? 'horizontal' : 'vertical');
        break;
    }
    update(ctx);
  }
};
