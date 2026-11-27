/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：Mapper 234的寄存器逻辑。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-17
 * 备注：寄存器写入后统一重新计算各窗口，与状态保持一致。
 * ------------------------------------------------------------------------
 */

function update(ctx) {
  const state = ctx.state;
  if ((state.reg0 & 0x40) !== 0) {
    // NINA-03 模式
    ctx.window('prg-8000-32k').setBank((state.reg0 & 0x0E) | (state.reg1 & 0x01));
    ctx.window('chr-0000').setBank(((state.reg0 << 2) & 0x38) | ((state.reg1 >> 4) & 0x07));
  } else {
    // CNROM 模式
    ctx.window('prg-8000-32k').setBank(state.reg0 & 0x0F);
    ctx.window('chr-0000').setBank(((state.reg0 << 2) & 0x3C) | ((state.reg1 >> 4) & 0x03));
  }
  ctx.nametable.set((state.reg0 & 0x80) !== 0 ? 'horizontal' : 'vertical');
}

// 读与写对寄存器的作用相同：读出的是总线上的字节，写入的是写入值。
function load(ctx, addr, value) {
  const state = ctx.state;
  if (addr <= 0xFF9F) {
    if ((state.reg0 & 0x3F) === 0) {
      state.reg0 = value;
      update(ctx);
    }
  } else {
    state.reg1 = value & 0x71;
    update(ctx);
  }
}

export default {
  reset(ctx, kind) {
    if (kind === 'power') update(ctx);
  },
  read(ctx, bus, addr) {
    const value = ctx.peek(addr);
    if (value !== undefined) load(ctx, addr, value);
    return value;
  },
  write(ctx, bus, addr, value) {
    load(ctx, addr, value);
  }
};
