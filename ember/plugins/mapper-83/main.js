/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：Mapper 83的寄存器逻辑。
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
const CHR_IDS = ['chr-0000', 'chr-0400', 'chr-0800', 'chr-0c00', 'chr-1000', 'chr-1400', 'chr-1800', 'chr-1c00'];
const regs = new Array(11).fill(0);
const ex = [0, 0, 0, 0];
let is2k = false;
let isNot2k = false;
let mode = 0;
let bank = 0;

function update(ctx) {
  ctx.nametable.set(['vertical', 'horizontal', 'single0', 'single1'][mode & 0x03]);
  const chr = (slot, page) => ctx.window(CHR_IDS[slot]).setBank(page);
  if (is2k && !isNot2k) {
    [[0, regs[0]], [2, regs[1]], [4, regs[6]], [6, regs[7]]].forEach(([slot, reg]) => {
      chr(slot, reg << 1);
      chr(slot + 1, (reg << 1) + 1);
    });
  } else {
    for (let index = 0; index < 8; index += 1) chr(index, regs[index] | ((bank & 0x30) << 4));
  }
  const prg = (slot, page) => {
    const window = ctx.window(PRG_IDS[slot]);
    window.setBank(page < 0 ? window.bankCount + page : page);
  };
  if (mode & 0x40) {
    const low = (bank & 0x3F) << 1;
    const high = ((bank & 0x30) | 0x0F) << 1;
    prg(0, low);
    prg(1, low + 1);
    prg(2, high);
    prg(3, high + 1);
  } else {
    prg(0, regs[8]);
    prg(1, regs[9]);
    prg(2, regs[10]);
    prg(3, -1);
  }
}

export default {
  reset(ctx) {
    regs.fill(0);
    ex.fill(0);
    is2k = false;
    isNot2k = false;
    mode = 0;
    bank = 0;
    cntSet(ctx, 0, false);
    update(ctx);
  },
  read(ctx, bus, addr) {
    if (addr === 0x5000) return parseInt(ctx.settings.get('dip'), 10) & 0x03;
    return ex[addr & 0x03];
  },
  write(ctx, bus, addr, value) {
    if (addr < 0x8000) {
      ex[addr & 0x03] = value;
    } else if (addr >= 0x8300 && addr <= 0x8302) {
      mode &= 0xBF;
      regs[addr - 0x8300 + 8] = value;
      update(ctx);
    } else if (addr >= 0x8310 && addr <= 0x8317) {
      regs[addr - 0x8310] = value;
      if (addr >= 0x8312 && addr <= 0x8315) isNot2k = true;
      update(ctx);
    } else if (addr === 0x8000 || addr === 0xB000 || addr === 0xB0FF || addr === 0xB1FF) {
      if (addr === 0x8000) is2k = true;
      bank = value;
      mode |= 0x40;
      update(ctx);
    } else if (addr === 0x8100) {
      mode = value | (mode & 0x40);
      update(ctx);
    } else if (addr === 0x8200) {
      cntSet(ctx, (cntLive(ctx) & 0xFF00) | value, cnt.enabled);
      ctx.irq.release();
    } else if (addr === 0x8201) {
      cntSet(ctx, (cntLive(ctx) & 0xFF) | (value << 8), (mode & 0x80) !== 0);
    }
  },
  onTimer(ctx) {
    cntFire(ctx);
  }
};
