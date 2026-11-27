/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：Mapper 35的寄存器逻辑。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-17
 * 备注：IRQ 计数在 PPU 地址线 A12 的上升沿（宿主调用 onA12，先低过 4 次显存访问再变高才算一次）。
 * ------------------------------------------------------------------------
 */

let counter = 0;
let enabled = false;

export default {
  reset() {
    counter = 0;
    enabled = false;
  },
  write(ctx, bus, addr, value) {
    const register = addr & 0xF007;
    if (register >= 0x8000 && register <= 0x8003) {
      ctx.window('prg-' + (0x8000 + (register & 0x03) * 0x2000).toString(16)).setBank(value);
    } else if (register >= 0x9000 && register <= 0x9007) {
      ctx.window('chr-' + ((register & 0x07) * 0x400).toString(16).padStart(4, '0')).setBank(value);
    } else if (register === 0xC002) {
      enabled = false;
      ctx.irq.release();
    } else if (register === 0xC003) {
      enabled = true;
    } else if (register === 0xC005) {
      counter = value;
    } else if (register === 0xD001) {
      ctx.nametable.set(value & 0x01 ? 'horizontal' : 'vertical');
    }
  },
  onA12(ctx) {
    if (!enabled) return;
    counter = (counter - 1) & 0xFF;
    if (counter === 0) {
      enabled = false;
      ctx.irq.assert();
    }
  }
};
