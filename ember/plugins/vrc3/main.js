/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：VRC3的寄存器逻辑。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-17
 * 备注：计数器按 CPU 周期在宿主调度器里计时：写寄存器时算出下一次溢出的时刻，到点回调 onTimer 拉起 IRQ，不逐周期运行脚本。
 * ------------------------------------------------------------------------
 */

let reloadValue = 0;
let enableOnAck = false;
let enabled = false;
let small = false;
let counter = 0;
let start = 0;

function live(ctx) {
  const elapsed = ctx.cycle() - start;
  return small ? (counter & 0xFF00) | ((counter + elapsed) & 0xFF) : (counter + elapsed) & 0xFFFF;
}

function reschedule(ctx) {
  ctx.timer.cancel('irq');
  if (!enabled) return;
  ctx.timer.after('irq', small ? 0x100 - (counter & 0xFF) : 0x10000 - counter);
}

export default {
  reset() {
    reloadValue = 0;
    enableOnAck = false;
    enabled = false;
    small = false;
    counter = 0;
    start = 0;
  },
  write(ctx, bus, addr, value) {
    switch (addr & 0xF000) {
      case 0x8000: reloadValue = (reloadValue & 0xFFF0) | (value & 0x0F); break;
      case 0x9000: reloadValue = (reloadValue & 0xFF0F) | ((value & 0x0F) << 4); break;
      case 0xA000: reloadValue = (reloadValue & 0xF0FF) | ((value & 0x0F) << 8); break;
      case 0xB000: reloadValue = (reloadValue & 0x0FFF) | ((value & 0x0F) << 12); break;
      case 0xC000:
        counter = live(ctx);
        enabled = (value & 0x02) !== 0;
        if (enabled) counter = reloadValue;
        small = (value & 0x04) !== 0;
        enableOnAck = (value & 0x01) !== 0;
        start = ctx.cycle();
        ctx.irq.release();
        reschedule(ctx);
        break;
      case 0xD000:
        counter = live(ctx);
        start = ctx.cycle();
        ctx.irq.release();
        enabled = enableOnAck;
        reschedule(ctx);
        break;
      case 0xF000: ctx.window('prg-8000-16k').setBank(value & 0x07); break;
    }
  },
  onTimer(ctx) {
    // 溢出：8 位计数只重载低 8 位，然后接着计数。
    counter = small ? (counter & 0xFF00) | (reloadValue & 0xFF) : reloadValue;
    start = ctx.cycle();
    ctx.irq.assert();
    reschedule(ctx);
  }
};
