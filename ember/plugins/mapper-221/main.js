/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：Mapper 221的寄存器逻辑。
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
  const outer = (state.mode & 0xFC) >> 2;
  if ((state.mode & 0x02) !== 0) {
    if ((state.mode & 0x0100) !== 0) {
      set16(ctx, outer | state.prgReg, outer | 0x07);
    } else {
      set16(ctx, outer | (state.prgReg & 0x06), (outer | (state.prgReg & 0x06)) + 1);
    }
  } else {
    set16(ctx, outer | state.prgReg, outer | state.prgReg);
  }
  ctx.nametable.set((state.mode & 0x01) !== 0 ? 'horizontal' : 'vertical');
}

export default {
  reset(ctx, kind) {
    if (kind === 'power') update(ctx);
  },
  write(ctx, bus, addr) {
    switch (addr & 0xC000) {
      case 0x8000:
        ctx.state.mode = addr;
        update(ctx);
        break;
      case 0xC000:
        ctx.state.prgReg = addr & 0x07;
        update(ctx);
        break;
    }
  }
};
