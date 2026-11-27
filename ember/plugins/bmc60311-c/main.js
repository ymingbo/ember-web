/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：BMC-60311C的寄存器逻辑。
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

function update(ctx) {
  const state = ctx.state;
  const page = state.outerPrg | ((state.mode & 0x04) !== 0 ? 0 : state.innerPrg);
  switch (state.mode & 0x03) {
    case 0:
      set16(ctx, page, page);
      break;
    case 1:
      set16(ctx, page & 0xFE, (page & 0xFE) + 1);
      break;
    case 2:
      set16(ctx, page, state.outerPrg | 7);
      break;
  }
  ctx.nametable.set((state.mode & 0x08) !== 0 ? 'horizontal' : 'vertical');
}

export default {
  reset(ctx, kind) {
    if (kind === 'power') update(ctx);
  },
  write(ctx, bus, addr, value) {
    const state = ctx.state;
    if (addr >= 0x8000) {
      state.innerPrg = value & 0x07;
    } else if ((addr & 0xE001) === 0x6000) {
      state.mode = value & 0x0F;
    } else if ((addr & 0xE001) === 0x6001) {
      state.outerPrg = value;
    } else {
      return;
    }
    update(ctx);
  }
};
