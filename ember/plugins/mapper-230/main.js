/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：Mapper 230的寄存器逻辑。
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
  reset(ctx, kind) {
    const state = ctx.state;
    // 上电先按合集模式清零，再像软复位那样切换一次，所以上电是魂斗罗模式。
    if (kind === 'power') state.contraMode = 0;
    state.contraMode ^= 1;
    if (state.contraMode !== 0) {
      set16(ctx, 0, 7);
      ctx.nametable.set('vertical');
    } else {
      set16(ctx, 8, 9);
      ctx.nametable.set('horizontal');
    }
  },
  write(ctx, bus, addr, value) {
    if (ctx.state.contraMode !== 0) {
      ctx.window('prg-8000-16k').setBank(value & 0x07);
      return;
    }
    if ((value & 0x20) !== 0) set16(ctx, (value & 0x1F) + 8, (value & 0x1F) + 8);
    else set16(ctx, (value & 0x1E) + 8, (value & 0x1E) + 9);
    ctx.nametable.set((value & 0x40) !== 0 ? 'vertical' : 'horizontal');
  }
};
