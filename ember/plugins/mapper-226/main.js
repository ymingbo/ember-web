/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：Mapper 226/233的寄存器逻辑。
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

// 按寄存器计算 PRG 窗口。
function updatePrg(ctx) {
  const state = ctx.state;
  const flag = ctx.header.mapper === 233 ? state.resetFlag << 5 : 0;
  const page = (state.reg0 & 0x1F) | ((state.reg0 & 0x80) >> 2) | ((state.reg1 & 0x01) << 6) | flag;
  if ((state.reg0 & 0x20) !== 0) set16(ctx, page, page);
  else set16(ctx, page & 0xFE, (page & 0xFE) + 1);
}

export default {
  reset(ctx, kind) {
    const state = ctx.state;
    if (kind === 'power') {
      state.resetFlag = 0;
      return;
    }
    state.reg0 = 0;
    state.reg1 = 0;
    set16(ctx, 0, 1);
    ctx.window('chr-0000').setBank(0);
    if (ctx.header.mapper === 233) {
      state.resetFlag ^= 1;
      updatePrg(ctx);
    }
  },
  write(ctx, bus, addr, value) {
    switch (addr & 0x8001) {
      case 0x8000:
        ctx.state.reg0 = value;
        break;
      case 0x8001:
        ctx.state.reg1 = value;
        break;
    }
    updatePrg(ctx);
    ctx.nametable.set((ctx.state.reg0 & 0x40) !== 0 ? 'vertical' : 'horizontal');
  }
};
