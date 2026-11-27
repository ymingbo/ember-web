/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：Mapper106的寄存器逻辑。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-17
 * 备注：计数器按 CPU 周期在宿主调度器里计时：写寄存器时算出下一次溢出的时刻，到点回调 onTimer 拉起 IRQ，不逐周期运行脚本。
 * ------------------------------------------------------------------------
 */

const PRG_IDS = ['prg-8000', 'prg-a000', 'prg-c000', 'prg-e000'];
const CHR_IDS = ['chr-0000', 'chr-0400', 'chr-0800', 'chr-0c00', 'chr-1000', 'chr-1400', 'chr-1800', 'chr-1c00'];
let enabled = false;
let base = 0;
let start = 0;

function live(ctx) {
  return enabled ? (base + (ctx.cycle() - start)) & 0xFFFF : base;
}

function reschedule(ctx) {
  ctx.timer.cancel('irq');
  if (enabled) ctx.timer.after('irq', 0x10000 - base);
}

export default {
  reset() {
    enabled = false;
    base = 0;
    start = 0;
  },
  write(ctx, bus, addr, value) {
    const register = addr & 0x0F;
    switch (register) {
      case 0: case 2: ctx.window(CHR_IDS[register]).setBank(value & 0xFE); break;
      case 1: case 3: ctx.window(CHR_IDS[register]).setBank(value | 0x01); break;
      case 4: case 5: case 6: case 7: ctx.window(CHR_IDS[register]).setBank(value); break;
      case 8: case 0x0B: ctx.window(PRG_IDS[register - 8]).setBank((value & 0x0F) | 0x10); break;
      case 9: case 0x0A: ctx.window(PRG_IDS[register - 8]).setBank(value & 0x1F); break;
      case 0x0D:
        enabled = false;
        base = 0;
        ctx.timer.cancel('irq');
        ctx.irq.release();
        break;
      case 0x0E:
        base = (live(ctx) & 0xFF00) | value;
        start = ctx.cycle();
        reschedule(ctx);
        break;
      case 0x0F:
        base = (live(ctx) & 0x00FF) | (value << 8);
        enabled = true;
        start = ctx.cycle();
        reschedule(ctx);
        break;
    }
  },
  onTimer(ctx) {
    enabled = false;
    base = 0;
    ctx.irq.assert();
  }
};
