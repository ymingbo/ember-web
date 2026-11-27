/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：Kaiser KS-7017的寄存器逻辑。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-17
 * 备注：计数器按 CPU 周期在宿主调度器里计时：写寄存器时算出下一次溢出的时刻，到点回调 onTimer 拉起 IRQ，不逐周期运行脚本。
 * ------------------------------------------------------------------------
 */

let prgReg = 0;
let mirroring = 'vertical';
let enabled = false;
let pending = false;
let base = 0;
let start = 0;

function live(ctx) {
  if (!enabled || base === 0) return base;
  return Math.max(0, base - (ctx.cycle() - start));
}

function reschedule(ctx) {
  ctx.timer.cancel('irq');
  if (enabled && base > 0) ctx.timer.after('irq', base);
}

export default {
  reset(ctx) {
    prgReg = 0;
    mirroring = 'vertical';
    enabled = false;
    pending = false;
    base = 0;
    start = 0;
    ctx.nametable.set('vertical');
  },
  read(ctx) {
    const was = pending;
    pending = false;
    ctx.irq.release();
    return was ? 0x01 : 0x00;
  },
  write(ctx, bus, addr, value) {
    if ((addr & 0xFF00) === 0x4A00) {
      prgReg = ((addr >> 2) & 0x03) | ((addr >> 4) & 0x04);
    } else if ((addr & 0xFF00) === 0x5100) {
      ctx.window('prg-8000-16k').setBank(prgReg);
      ctx.nametable.set(mirroring);
    } else if (addr === 0x4020) {
      pending = false;
      ctx.irq.release();
      base = (live(ctx) & 0xFF00) | value;
      start = ctx.cycle();
      reschedule(ctx);
    } else if (addr === 0x4021) {
      pending = false;
      ctx.irq.release();
      base = (live(ctx) & 0x00FF) | (value << 8);
      enabled = true;
      start = ctx.cycle();
      reschedule(ctx);
    } else if (addr === 0x4025) {
      mirroring = (value >> 3) & 0x01 ? 'horizontal' : 'vertical';
    }
  },
  onTimer(ctx) {
    enabled = false;
    base = 0;
    pending = true;
    ctx.irq.assert();
  }
};
