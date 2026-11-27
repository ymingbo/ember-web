/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：VRC2 / VRC4的寄存器逻辑。
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
// 变体：2a、2b、2c 是 VRC2，其余是 VRC4。
const VARIANTS = {
  21: { 0: '4a', 1: '4a', 2: '4c' },
  22: { 0: '2a' },
  23: { 0: '2b', 1: '4f', 2: '4e', 3: '2b' },
  25: { 0: '4b', 1: '4b', 2: '4d', 3: '2c' },
  27: { 0: '4_27' },
  183: { 0: '4_183' },
};
let variant = '4a';
let heuristics = false;
let prgReg0 = 0;
let prgReg1 = 0;
let prgMode = 0;
let latch = 0;
const lo = [0, 0, 0, 0, 0, 0, 0, 0];
const hi = [0, 0, 0, 0, 0, 0, 0, 0];

function isVrc2() {
  return variant === '2a' || variant === '2b' || variant === '2c';
}

function translate(addr) {
  let a0 = 0;
  let a1 = 0;
  const bit = (shift) => (addr >> shift) & 0x01;
  if (heuristics) {
    switch (variant) {
      case '2c': case '4b': case '4d': a0 = bit(1) | bit(3); a1 = bit(0) | bit(2); break;
      case '4a': case '4c': a0 = bit(1) | bit(6); a1 = bit(2) | bit(7); break;
      default: a0 = bit(0) | bit(2); a1 = bit(1) | bit(3); break;
    }
  } else {
    switch (variant) {
      case '2a': a0 = bit(1); a1 = bit(0); break;
      case '4_27': a0 = bit(0); a1 = bit(1); break;
      case '2c': case '4b': a0 = bit(1); a1 = bit(0); break;
      case '4d': a0 = bit(3); a1 = bit(2); break;
      case '4a': a0 = bit(1); a1 = bit(2); break;
      case '4c': a0 = bit(6); a1 = bit(7); break;
      case '2b': case '4f': a0 = bit(0); a1 = bit(1); break;
      default: a0 = bit(2); a1 = bit(3); break;
    }
  }
  return (addr & 0xFF00) | (a1 << 1) | a0;
}

function update(ctx) {
  CHR_IDS.forEach((id, index) => {
    let page = lo[index] | (hi[index] << 4);
    if (variant === '2a') page >>= 1;
    ctx.window(id).setBank(page);
  });
  const setPage = (id, page) => {
    const window = ctx.window(id);
    window.setBank(page < 0 ? window.bankCount + page : page);
  };
  if (prgMode === 0) {
    setPage('prg-8000', prgReg0);
    setPage('prg-a000', prgReg1);
    setPage('prg-c000', -2);
    setPage('prg-e000', -1);
  } else {
    setPage('prg-8000', -2);
    setPage('prg-a000', prgReg1);
    setPage('prg-c000', prgReg0);
    setPage('prg-e000', -1);
  }
}

function latchMode(ctx) {
  const header = ctx.header;
  const noRam = !header.nes20 || ((header.prgRamSize ?? 0) === 0 && (header.prgNvramSize ?? 0) === 0);
  return !heuristics && isVrc2() && noRam;
}

export default {
  reset(ctx, kind) {
    const mapper = ctx.header.mapper;
    variant = (VARIANTS[mapper] ?? VARIANTS[21])[ctx.header.variant] ?? (VARIANTS[mapper] ?? VARIANTS[21])[0];
    heuristics = ctx.header.variant === 0 && mapper !== 22 && mapper !== 27 && mapper !== 183;
    prgMode = 0;
    prgReg0 = 0;
    prgReg1 = 0;
    latch = 0;
    lo.fill(0);
    hi.fill(0);
    vrcReset();
    if (variant === '4_183') {
      ctx.window('prg-6000').select('prg', 0);
      ctx.window('prg-6000').setAccess('r');
    }
    update(ctx);
  },
  read(ctx, bus, addr) {
    if (latchMode(ctx)) return latch;
    return ctx.peek(addr);
  },
  write(ctx, bus, addr, value) {
    if (addr < 0x8000) {
      if (variant === '4_183') {
        ctx.window('prg-6000').setBank(addr & 0x0F);
      } else if (latchMode(ctx)) {
        latch = value & 0x01;
      } else {
        ctx.storage('prg-ram').write(addr & 0x1FFF, value);
      }
      return;
    }
    const register = translate(addr) & 0xF00F;
    const vrc4 = !isVrc2();
    if (register >= 0x8000 && register <= 0x8006) {
      prgReg0 = value & 0x1F;
    } else if ((!vrc4 && register >= 0x9000 && register <= 0x9003) || (vrc4 && register >= 0x9000 && register <= 0x9001)) {
      const mask = !heuristics && isVrc2() ? 0x01 : 0x03;
      ctx.nametable.set(['vertical', 'horizontal', 'single0', 'single1'][value & mask]);
    } else if (vrc4 && register >= 0x9002 && register <= 0x9003) {
      prgMode = (value >> 1) & 0x01;
    } else if (register >= 0xA000 && register <= 0xA006) {
      prgReg1 = value & 0x1F;
    } else if (register >= 0xB000 && register <= 0xE006) {
      const number = ((((register >> 12) & 0x07) - 3) << 1) + ((register >> 1) & 0x01);
      if ((register & 0x01) === 0) lo[number] = value & 0x0F;
      else hi[number] = value & 0x1F;
    } else if (vrc4 || (heuristics && ctx.header.mapper !== 22)) {
      if (register === 0xF000) vrc.reload = (vrc.reload & 0xF0) | (value & 0x0F);
      else if (register === 0xF001) vrc.reload = (vrc.reload & 0x0F) | ((value & 0x0F) << 4);
      else if (register === 0xF002) vrcControl(ctx, value);
      else if (register === 0xF003) vrcAck(ctx);
    }
    update(ctx);
  },
  onTimer(ctx) {
    vrcFire(ctx);
  }
};
