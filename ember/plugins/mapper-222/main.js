/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：Mapper 222的寄存器逻辑。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-17
 * 备注：IRQ 计数在 PPU 地址线 A12 的上升沿（宿主调用 onA12，先低过 4 次显存访问再变高才算一次）。
 * ------------------------------------------------------------------------
 */

let counter = 0;
const CHR_REGISTER = { 0xB000: 0, 0xB002: 1, 0xC000: 2, 0xC002: 3, 0xD000: 4, 0xD002: 5, 0xE000: 6, 0xE002: 7 };

export default {
  reset() {
    counter = 0;
  },
  write(ctx, bus, addr, value) {
    const register = addr & 0xF003;
    if (register === 0x8000) {
      ctx.window('prg-8000').setBank(value);
    } else if (register === 0xA000) {
      ctx.window('prg-a000').setBank(value);
    } else if (register === 0x9000) {
      ctx.nametable.set(value & 0x01 ? 'horizontal' : 'vertical');
    } else if (register in CHR_REGISTER) {
      ctx.window('chr-' + (CHR_REGISTER[register] * 0x400).toString(16).padStart(4, '0')).setBank(value);
    } else if (register === 0xF000) {
      counter = value;
      ctx.irq.release();
    }
  },
  onA12(ctx) {
    if (counter === 0) return;
    counter += 1;
    if (counter >= 240) {
      ctx.irq.assert();
      counter = 0;
    }
  }
};
