/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：TF1201的寄存器逻辑。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-17
 * 备注：计数器按 CPU 周期内的预分频（每周期 3、满 341 记一次）累加：写寄存器时算出下一次溢出的时刻，到点回调 onTimer 拉起 IRQ，不逐周期运行脚本。
 * ------------------------------------------------------------------------
 */

const PRG_IDS = ['prg-8000', 'prg-a000', 'prg-c000', 'prg-e000'];
const CHR_IDS = ['chr-0000', 'chr-0400', 'chr-0800', 'chr-0c00', 'chr-1000', 'chr-1400', 'chr-1800', 'chr-1c00'];
const chr = new Array(8).fill(0);
const prgRegs = [0, 0];
let swapPrg = false;
const irq = { reload: 0, counter: 0, prescaler: 0, enabled: false, next: 0 };

function toFire() {
  const ticks = 0x100 - irq.counter;
  let prescaler = irq.prescaler;
  let cycles = 0;
  for (let tick = 0; tick < ticks; tick += 1) {
    const step = Math.ceil(prescaler / 3);
    cycles += step;
    prescaler = prescaler - 3 * step + 341;
  }
  return { cycles, prescaler };
}

function schedule(ctx) {
  ctx.timer.cancel('irq');
  if (!irq.enabled) return;
  const result = toFire();
  irq.next = result.prescaler;
  ctx.timer.after('irq', Math.max(1, result.cycles));
}

function update(ctx) {
  const prg = (slot, page) => {
    const window = ctx.window(PRG_IDS[slot]);
    window.setBank(page < 0 ? window.bankCount + page : page);
  };
  prg(0, swapPrg ? -2 : prgRegs[0]);
  prg(1, prgRegs[1]);
  prg(2, swapPrg ? prgRegs[0] : -2);
  prg(3, -1);
  CHR_IDS.forEach((id, index) => ctx.window(id).setBank(chr[index]));
}

export default {
  reset(ctx) {
    chr.fill(0);
    prgRegs.fill(0);
    swapPrg = false;
    irq.reload = 0;
    irq.counter = 0;
    irq.prescaler = 0;
    irq.enabled = false;
    ctx.timer.cancel('irq');
    update(ctx);
  },
  write(ctx, bus, addr, value) {
    const register = (addr & 0xF003) | ((addr & 0x0C) >> 2);
    if (register >= 0xB000 && register <= 0xE003) {
      const slot = (((register >> 11) - 6) | (register & 0x01)) & 0x07;
      const shift = (register & 0x02) << 1;
      chr[slot] = (chr[slot] & (0xF0 >> shift)) | ((value & 0x0F) << shift);
      update(ctx);
      return;
    }
    switch (register & 0xF003) {
      case 0x8000: prgRegs[0] = value; update(ctx); break;
      case 0xA000: prgRegs[1] = value; update(ctx); break;
      case 0x9000: ctx.nametable.set(value & 0x01 ? 'horizontal' : 'vertical'); break;
      case 0x9001: swapPrg = (value & 0x03) !== 0; update(ctx); break;
      case 0xF000: irq.reload = (irq.reload & 0xF0) | (value & 0x0F); break;
      case 0xF002: irq.reload = (irq.reload & 0x0F) | (value << 4); break;
      case 0xF001:
        irq.enabled = (value & 0x02) !== 0;
        if (irq.enabled) {
          irq.prescaler = 341;
          irq.counter = irq.reload;
        }
        ctx.irq.release();
        schedule(ctx);
        break;
      case 0xF003: ctx.irq.release(); break;
    }
  },
  onTimer(ctx) {
    ctx.irq.assert();
    irq.counter = 0;
    irq.prescaler = irq.next;
    schedule(ctx);
  }
};
