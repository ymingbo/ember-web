/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：Irem G-101的寄存器逻辑。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-17
 * 备注：寄存器写入后统一重新计算各窗口，与状态保持一致。
 * ------------------------------------------------------------------------
 */

const CHR = ["chr-0000","chr-0400","chr-0800","chr-0c00","chr-1000","chr-1400","chr-1800","chr-1c00"];

// 按 PRG 模式摆放四个 8K 窗口：-2、-1 是倒数第二个与最后一个 bank。
function updatePrgMode(ctx) {
  const state = ctx.state;
  const count = ctx.window('prg-c000').bankCount;
  if (state.prgMode === 0) {
    ctx.window('prg-8000').setBank(state.prgReg0);
    ctx.window('prg-a000').setBank(state.prgReg1);
    ctx.window('prg-c000').setBank(count - 2);
  } else {
    ctx.window('prg-8000').setBank(count - 2);
    ctx.window('prg-a000').setBank(state.prgReg1);
    ctx.window('prg-c000').setBank(state.prgReg0);
  }
  ctx.window('prg-e000').setBank(count - 1);
}

export default {
  reset(ctx, kind) {
    if (kind === 'power' && ctx.header.variant === 1) ctx.nametable.set('single0');
  },
  write(ctx, bus, addr, value) {
    const state = ctx.state;
    switch (addr & 0xF000) {
      case 0x8000:
        state.prgReg0 = value & 0x1F;
        ctx.window(state.prgMode === 0 ? 'prg-8000' : 'prg-c000').setBank(state.prgReg0);
        break;
      case 0x9000:
        state.prgMode = ctx.header.variant === 1 ? 0 : (value & 0x02) >> 1;
        updatePrgMode(ctx);
        ctx.nametable.set((value & 0x01) !== 0 ? 'horizontal' : 'vertical');
        break;
      case 0xA000:
        state.prgReg1 = value & 0x1F;
        ctx.window('prg-a000').setBank(state.prgReg1);
        break;
      case 0xB000:
        ctx.window(CHR[addr & 0x07]).setBank(value);
        break;
    }
  }
};
