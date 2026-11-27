/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：Irem H3001的寄存器逻辑。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-17
 * 备注：计数器按 CPU 周期在宿主调度器里计时：写寄存器时算出下一次溢出的时刻，到点回调 onTimer 拉起 IRQ，不逐周期运行脚本。
 * ------------------------------------------------------------------------
 */

let enabled = false;
let counter = 0;
let reload = 0;
let start = 0;

function live(ctx) {
  return enabled ? (counter - (ctx.cycle() - start)) & 0xFFFF : counter;
}

function reschedule(ctx) {
  ctx.timer.cancel('irq');
  if (enabled) ctx.timer.after('irq', counter === 0 ? 0x10000 : counter);
}

export default {
  reset() {
    enabled = false;
    counter = 0;
    reload = 0;
    start = 0;
  },
  write(ctx, bus, addr, value) {
    switch (addr) {
      case 0x8000: ctx.window('prg-8000').setBank(value); break;
      case 0x9001: ctx.nametable.set(value & 0x80 ? 'horizontal' : 'vertical'); break;
      case 0x9003:
        counter = live(ctx);
        enabled = (value & 0x80) !== 0;
        start = ctx.cycle();
        ctx.irq.release();
        reschedule(ctx);
        break;
      case 0x9004:
        counter = reload;
        start = ctx.cycle();
        ctx.irq.release();
        reschedule(ctx);
        break;
      case 0x9005: reload = (reload & 0x00FF) | (value << 8); break;
      case 0x9006: reload = (reload & 0xFF00) | value; break;
      case 0xA000: ctx.window('prg-a000').setBank(value); break;
      case 0xC000: ctx.window('prg-c000').setBank(value); break;
      default:
        if (addr >= 0xB000 && addr <= 0xB007) ctx.window('chr-' + ((addr & 0x07) * 0x400).toString(16).padStart(4, '0')).setBank(value);
    }
  },
  onTimer(ctx) {
    enabled = false;
    counter = 0;
    ctx.irq.assert();
  }
};
