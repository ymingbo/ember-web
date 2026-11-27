/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：Subor 166/167的寄存器逻辑。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-17
 * 备注：寄存器写入后统一重新计算各窗口，与状态保持一致。
 * ------------------------------------------------------------------------
 */

function update(ctx) {
  const state = ctx.state;
  const alt = ctx.header.mapper === 167;
  const outer = ((state.reg0 ^ state.reg1) & 0x10) << 1;
  const inner = state.reg2 ^ state.reg3;
  const low = ctx.window('prg-8000-16k');
  const high = ctx.window('prg-c000-16k');
  if ((state.reg1 & 0x08) !== 0) {
    // 32K NROM
    const bank = (outer | inner) & 0xFE;
    low.setBank(alt ? bank + 1 : bank);
    high.setBank(alt ? bank : bank + 1);
  } else if ((state.reg1 & 0x04) !== 0) {
    // 512K 反向 UNROM
    low.setBank(0x1F);
    high.setBank(outer | inner);
  } else {
    // 512K UNROM
    low.setBank(outer | inner);
    high.setBank(alt ? 0x20 : 0x07);
  }
}

export default {
  reset(ctx, kind) {
    if (kind !== 'power') return;
    for (let index = 0; index < 4; index += 1) ctx.state['reg' + index] = 0;
    update(ctx);
  },
  write(ctx, bus, addr, value) {
    const state = ctx.state;
    switch (addr & 0xE000) {
      case 0x8000:
        state.reg0 = value & 0x10;
        break;
      case 0xA000:
        state.reg1 = value & 0x1C;
        break;
      case 0xC000:
        state.reg2 = value & 0x1F;
        break;
      case 0xE000:
        state.reg3 = value & 0x1F;
        break;
    }
    update(ctx);
  }
};
