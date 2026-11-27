/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：Mapper 117的寄存器逻辑。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-17
 * 备注：IRQ 计数在 PPU 地址线 A12 的上升沿（宿主调用 onA12，先低过 4 次显存访问再变高才算一次）。
 * ------------------------------------------------------------------------
 */

let counter = 0;
let reload = 0;
let enabled = false;
let enabledAlt = false;

export default {
  reset() {
    counter = 0;
    reload = 0;
    enabled = false;
    enabledAlt = false;
  },
  write(ctx, bus, addr, value) {
    if (addr >= 0x8000 && addr <= 0x8003) {
      const window = ctx.window('prg-' + (0x8000 + (addr & 0x03) * 0x2000).toString(16));
      window.setBank(value);
    } else if (addr >= 0xA000 && addr <= 0xA007) {
      ctx.window('chr-' + ((addr & 0x07) * 0x400).toString(16).padStart(4, '0')).setBank(value);
    } else if (addr === 0xC001) {
      reload = value;
    } else if (addr === 0xC002) {
      ctx.irq.release();
    } else if (addr === 0xC003) {
      counter = reload;
      enabledAlt = true;
    } else if (addr === 0xD000) {
      ctx.nametable.set(value & 0x01 ? 'horizontal' : 'vertical');
    } else if (addr === 0xE000) {
      enabled = (value & 0x01) !== 0;
      ctx.irq.release();
    }
  },
  onA12(ctx) {
    if (enabled && enabledAlt && counter !== 0) {
      counter -= 1;
      if (counter === 0) {
        ctx.irq.assert();
        enabledAlt = false;
      }
    }
  }
};
