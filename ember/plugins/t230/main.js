/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：T230的寄存器逻辑。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-17
 * 备注：只有 CHR RAM 的文件（头里没有 CHR ROM）把图案窗口固定指向前 8K，并用 $B000 起的写入选程序外部组。
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

const PRG_IDS = ['prg-8000', 'prg-a000', 'prg-c000', 'prg-e000'];
const CHR_IDS = ['chr-0000', 'chr-0400', 'chr-0800', 'chr-0c00', 'chr-1000', 'chr-1400', 'chr-1800', 'chr-1c00'];
const lo = new Array(8).fill(0);
const hi = new Array(8).fill(0);
let prgReg0 = 0;
let prgReg1 = 0;
let prgMode = 0;
let outer = 0;

function update(ctx) {
  const chrRam = ctx.header.chrSize === 0;
  CHR_IDS.forEach((id, index) => ctx.window(id).setBank(chrRam ? index : lo[index] | (hi[index] << 4)));
  const prg = (slot, page) => {
    const window = ctx.window(PRG_IDS[slot]);
    window.setBank(page < 0 ? window.bankCount + page : page);
  };
  if (prgMode === 0) {
    prg(0, prgReg0 | outer);
    prg(2, 0x1E | outer);
  } else {
    prg(0, 0x1E | outer);
    prg(2, prgReg0 | outer);
  }
  prg(1, prgReg1);
  prg(3, -1);
}

export default {
  reset(ctx) {
    lo.fill(0);
    hi.fill(0);
    prgReg0 = 0;
    prgReg1 = 0;
    prgMode = 0;
    outer = 0;
    vrcReset();
    update(ctx);
  },
  write(ctx, bus, rawAddr, value) {
    const addr = (rawAddr & 0xF000) | (rawAddr & 0x2A ? 0x02 : 0) | (rawAddr & 0x15 ? 0x01 : 0);
    if (addr >= 0x9000 && addr <= 0x9001) {
      if (value <= 3) ctx.nametable.set(['vertical', 'horizontal', 'single0', 'single1'][value]);
    } else if (addr >= 0x9002 && addr <= 0x9003) {
      prgMode = (value >> 1) & 0x01;
    } else if (addr >= 0xA000 && addr <= 0xA003) {
      prgReg0 = (value & 0x1F) << 1;
      prgReg1 = ((value & 0x1F) << 1) | 0x01;
    } else if (addr >= 0xB000 && addr <= 0xE003) {
      if (ctx.header.chrSize === 0) {
        outer = (value & 0x08) << 2;
      } else {
        const number = ((((addr >> 12) & 0x07) - 3) << 1) + ((addr >> 1) & 0x01);
        if ((addr & 0x01) === 0) lo[number] = value & 0x0F;
        else hi[number] = value & 0x1F;
      }
    } else if (addr === 0xF000) {
      vrc.reload = (vrc.reload & 0xF0) | (value & 0x0F);
    } else if (addr === 0xF001) {
      vrc.reload = (vrc.reload & 0x0F) | ((value & 0x0F) << 4);
    } else if (addr === 0xF002) {
      vrcControl(ctx, value);
    } else if (addr === 0xF003) {
      vrcAck(ctx);
    }
    update(ctx);
  },
  onTimer(ctx) {
    vrcFire(ctx);
  }
};
