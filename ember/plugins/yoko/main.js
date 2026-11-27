/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：Yoko的寄存器逻辑。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-17
 * 备注：计数器按 CPU 周期递减：写寄存器时算出下一次减到 0 的时刻，到点回调 onTimer 拉起 IRQ，不逐周期运行脚本。
 * ------------------------------------------------------------------------
 */

const cnt = { value: 0, enabled: false, start: 0 };

function cntLive(ctx) {
  return cnt.enabled ? (cnt.value - (ctx.cycle() - cnt.start)) & 0xFFFF : cnt.value;
}

function cntSet(ctx, value, enabled) {
  cnt.value = value & 0xFFFF;
  cnt.enabled = enabled;
  cnt.start = ctx.cycle();
  ctx.timer.cancel('irq');
  if (enabled) ctx.timer.after('irq', cnt.value === 0 ? 0x10000 : cnt.value);
}

function cntFire(ctx) {
  cnt.enabled = false;
  cnt.value = 0xFFFF;
  ctx.irq.assert();
}

const PRG_IDS = ['prg-8000', 'prg-a000', 'prg-c000', 'prg-e000'];
const CHR_IDS = ['chr-0000', 'chr-0800', 'chr-1000', 'chr-1800'];
const regs = new Array(7).fill(0);
const ex = [0, 0, 0, 0];
let mode = 0;
let bank = 0;

function update(ctx) {
  ctx.nametable.set(mode & 0x01 ? 'horizontal' : 'vertical');
  for (let index = 0; index < 4; index += 1) ctx.window(CHR_IDS[index]).setBank(regs[3 + index]);
  const prg = (slot, page) => {
    const window = ctx.window(PRG_IDS[slot]);
    window.setBank(page < 0 ? window.bankCount + page : page);
  };
  if (mode & 0x10) {
    const outer = (bank & 0x08) << 1;
    prg(0, outer | (regs[0] & 0x0F));
    prg(1, outer | (regs[1] & 0x0F));
    prg(2, outer | (regs[2] & 0x0F));
    prg(3, outer | 0x0F);
  } else if (mode & 0x08) {
    const base = (bank & 0xFE) << 1;
    for (let index = 0; index < 4; index += 1) prg(index, base + index);
  } else {
    prg(0, bank << 1);
    prg(1, (bank << 1) + 1);
    prg(2, -2);
    prg(3, -1);
  }
}

export default {
  reset(ctx, kind) {
    if (kind === 'power') {
      regs.fill(0);
      ex.fill(0);
      cntSet(ctx, 0, false);
    }
    mode = 0;
    bank = 0;
    update(ctx);
  },
  read(ctx, bus, addr) {
    if (addr <= 0x53FF) return parseInt(ctx.settings.get('dip'), 10) & 0x03;
    return ex[addr & 0x03];
  },
  write(ctx, bus, addr, value) {
    if (addr < 0x8000) {
      ex[addr & 0x03] = value;
      return;
    }
    switch (addr & 0x8C17) {
      case 0x8000: bank = value; update(ctx); break;
      case 0x8400: mode = value; update(ctx); break;
      case 0x8800:
        cntSet(ctx, (cntLive(ctx) & 0xFF00) | value, cnt.enabled);
        ctx.irq.release();
        break;
      case 0x8801: cntSet(ctx, (cntLive(ctx) & 0xFF) | (value << 8), (mode & 0x80) !== 0); break;
      case 0x8C00: regs[0] = value; update(ctx); break;
      case 0x8C01: regs[1] = value; update(ctx); break;
      case 0x8C02: regs[2] = value; update(ctx); break;
      case 0x8C10: regs[3] = value; update(ctx); break;
      case 0x8C11: regs[4] = value; update(ctx); break;
      case 0x8C16: regs[5] = value; update(ctx); break;
      case 0x8C17: regs[6] = value; update(ctx); break;
    }
  },
  onTimer(ctx) {
    cntFire(ctx);
  }
};
