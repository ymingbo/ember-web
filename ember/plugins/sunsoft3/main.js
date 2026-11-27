/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：Sunsoft 3的寄存器逻辑。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-17
 * 备注：计数器按 CPU 周期在宿主调度器里计时：写寄存器时算出下一次溢出的时刻，到点回调 onTimer 拉起 IRQ，不逐周期运行脚本。
 * ------------------------------------------------------------------------
 */

const CHR_2K = ['chr-0000', 'chr-0800', 'chr-1000', 'chr-1800'];
const MIRRORING = ['vertical', 'horizontal', 'single0', 'single1'];
let latch = false;
let enabled = false;
let base = 0;
let start = 0;

/** 计数器现在的值：允许时从 base 起每周期减 1。 */
function live(ctx) {
  return enabled ? (base - (ctx.cycle() - start)) & 0xFFFF : base;
}

function reschedule(ctx) {
  ctx.timer.cancel('irq');
  if (enabled) ctx.timer.after('irq', base + 1);
}

export default {
  reset() {
    latch = false;
    enabled = false;
    base = 0;
    start = 0;
  },
  write(ctx, bus, addr, value) {
    switch (addr & 0xF800) {
      case 0x8800: setPage(ctx, 0, value); break;
      case 0x9800: setPage(ctx, 1, value); break;
      case 0xA800: setPage(ctx, 2, value); break;
      case 0xB800: setPage(ctx, 3, value); break;
      case 0xC800: {
        const current = live(ctx);
        base = latch ? (current & 0xFF00) | value : (current & 0x00FF) | (value << 8);
        latch = !latch;
        start = ctx.cycle();
        reschedule(ctx);
        break;
      }
      case 0xD800:
        base = live(ctx);
        enabled = (value & 0x10) !== 0;
        latch = false;
        start = ctx.cycle();
        ctx.irq.release();
        reschedule(ctx);
        break;
      case 0xE800: ctx.nametable.set(MIRRORING[value & 0x03]); break;
      case 0xF800: ctx.window('prg-8000-16k').setBank(value); break;
    }
  },
  onTimer(ctx) {
    enabled = false;
    base = 0xFFFF;
    ctx.irq.assert();
  }
};

function setPage(ctx, slot, value) {
  ctx.window(CHR_2K[slot]).setBank(value);
}
