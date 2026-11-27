/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：Waixing 252的寄存器逻辑。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-17
 * 备注：计数器按 CPU 周期在宿主调度器里计时：写寄存器时算出下一次溢出的时刻，到点回调 onTimer 拉起 IRQ，不逐周期运行脚本。
 * ------------------------------------------------------------------------
 */

const vrc = { reload: 0, counter: 0, prescaler: 0, enabled: false, afterAck: false, cycleMode: false, next: 0 };

/** 从当前计数器与预分频器算起，到下一次发出 IRQ 要多少个 CPU 周期，以及那时的预分频值。 */
function vrcToFire() {
  const ticks = 0x100 - vrc.counter;
  if (vrc.cycleMode) return { cycles: ticks, prescaler: vrc.prescaler };
  let prescaler = vrc.prescaler;
  let cycles = 0;
  for (let tick = 0; tick < ticks; tick += 1) {
    const step = Math.ceil(prescaler / 3);
    cycles += step;
    prescaler = prescaler - 3 * step + 341;
  }
  return { cycles, prescaler };
}

function vrcSchedule(ctx) {
  ctx.timer.cancel('irq');
  if (!vrc.enabled) return;
  const result = vrcToFire();
  vrc.next = result.prescaler;
  ctx.timer.after('irq', Math.max(1, result.cycles));
}

function vrcReset() {
  vrc.reload = 0;
  vrc.counter = 0;
  vrc.prescaler = 0;
  vrc.enabled = false;
  vrc.afterAck = false;
  vrc.cycleMode = false;
}

function vrcControl(ctx, value) {
  vrc.afterAck = (value & 0x01) !== 0;
  vrc.enabled = (value & 0x02) !== 0;
  vrc.cycleMode = (value & 0x04) !== 0;
  if (vrc.enabled) {
    vrc.counter = vrc.reload;
    vrc.prescaler = 341;
  }
  ctx.irq.release();
  vrcSchedule(ctx);
}

function vrcAck(ctx) {
  const was = vrc.enabled;
  vrc.enabled = vrc.afterAck;
  ctx.irq.release();
  if (!was && vrc.enabled) vrcSchedule(ctx);
  if (was && !vrc.enabled) ctx.timer.cancel('irq');
}

function vrcFire(ctx) {
  ctx.irq.assert();
  vrc.counter = vrc.reload;
  vrc.prescaler = vrc.next;
  vrcSchedule(ctx);
}

const CHR_IDS = ['chr-0000', 'chr-0400', 'chr-0800', 'chr-0c00', 'chr-1000', 'chr-1400', 'chr-1800', 'chr-1c00'];
const chr = [0, 0, 0, 0, 0, 0, 0, 0];

export default {
  reset() {
    chr.fill(0);
    vrcReset();
  },
  write(ctx, bus, addr, value) {
    if (addr <= 0x8FFF) {
      ctx.window('prg-8000').setBank(value);
    } else if (addr >= 0xA000 && addr <= 0xAFFF) {
      ctx.window('prg-a000').setBank(value);
    } else if (addr >= 0xB000 && addr <= 0xEFFF) {
      const shift = addr & 0x04;
      const bank = ((((addr - 0xB000) >> 1) & 0x1800) | ((addr << 7) & 0x0400)) / 0x400;
      chr[bank] = (chr[bank] & (0xF0 >> shift)) | ((value & 0x0F) << shift);
      ctx.window(CHR_IDS[bank]).setBank(chr[bank]);
    } else {
      switch (addr & 0xF00C) {
        case 0xF000: vrc.reload = (vrc.reload & 0xF0) | (value & 0x0F); break;
        case 0xF004: vrc.reload = (vrc.reload & 0x0F) | ((value & 0x0F) << 4); break;
        case 0xF008: vrcControl(ctx, value); break;
        case 0xF00C: vrcAck(ctx); break;
      }
    }
  },
  onTimer(ctx) {
    vrcFire(ctx);
  }
};
