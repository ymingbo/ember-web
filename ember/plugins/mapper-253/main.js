/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：Mapper253的寄存器逻辑。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-17
 * 备注：计数器按 CPU 周期在宿主调度器里计时：写寄存器时算出下一次溢出的时刻，到点回调 onTimer 拉起 IRQ，不逐周期运行脚本。
 * ------------------------------------------------------------------------
 */

const CHR_IDS = ['chr-0000', 'chr-0400', 'chr-0800', 'chr-0c00', 'chr-1000', 'chr-1400', 'chr-1800', 'chr-1c00'];
const chrLow = [0, 0, 0, 0, 0, 0, 0, 0];
const chrHigh = [0, 0, 0, 0, 0, 0, 0, 0];
let forceChrRom = false;
let reload = 0;
let enabled = false;
let counter = 0;

function updateChr(ctx) {
  CHR_IDS.forEach((id, index) => {
    const page = chrLow[index] | (chrHigh[index] << 8);
    const window = ctx.window(id);
    if ((chrLow[index] === 4 || chrLow[index] === 5) && !forceChrRom) window.select('chr-ram', page & 0x01);
    else window.select('chr', page);
  });
}

function schedule(ctx) {
  ctx.timer.cancel('irq');
  if (enabled) ctx.timer.after('irq', 114 * (0x100 - counter));
}

export default {
  reset(ctx) {
    chrLow.fill(0);
    chrHigh.fill(0);
    forceChrRom = false;
    reload = 0;
    enabled = false;
    counter = 0;
    updateChr(ctx);
  },
  write(ctx, bus, addr, value) {
    if (addr >= 0xB000 && addr <= 0xE00C) {
      const slot = (((((addr & 0x08) | (addr >> 8)) >> 3) + 2) & 0x07);
      const shift = addr & 0x04;
      const low = ((chrLow[slot] & (0xF0 >> shift)) | (value << shift)) & 0xFF;
      chrLow[slot] = low;
      if (slot === 0) {
        if (low === 0xC8) forceChrRom = false;
        else if (low === 0x88) forceChrRom = true;
      }
      if (shift) chrHigh[slot] = value >> 4;
      updateChr(ctx);
      return;
    }
    switch (addr) {
      case 0x8010: ctx.window('prg-8000').setBank(value); break;
      case 0xA010: ctx.window('prg-a000').setBank(value); break;
      case 0x9400: ctx.nametable.set(['vertical', 'horizontal', 'single0', 'single1'][value & 0x03]); break;
      case 0xF000:
        reload = (reload & 0xF0) | (value & 0x0F);
        ctx.irq.release();
        break;
      case 0xF004:
        reload = (reload & 0x0F) | ((value << 4) & 0xF0);
        ctx.irq.release();
        break;
      case 0xF008:
        counter = reload;
        enabled = (value & 0x02) !== 0;
        ctx.irq.release();
        schedule(ctx);
        break;
    }
  },
  onTimer(ctx) {
    counter = reload;
    ctx.irq.assert();
    schedule(ctx);
  }
};
