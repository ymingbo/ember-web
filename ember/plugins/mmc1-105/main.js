/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：MMC1（mapper 105）的寄存器逻辑。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-17
 * 备注：IRQ 计数器按 CPU 周期递增：开始计时时登记一个计时器，到点回调 onTimer；关闭时取消。
 * ------------------------------------------------------------------------
 */

const MIRRORING = ['single0', 'single1', 'vertical', 'horizontal'];
let lastWriteCycle = -1000;

/** 计数器的最大值（CPU 周期）：高位固定，DIP 开关决定位 28-25。 */
function maxCounter(ctx) {
  return 0x20000000 + ((parseInt(ctx.settings.get('dip'), 10) & 0x0F) * 0x2000000);
}

let initState = 0;
let irqEnabled = false;

/** 按当前寄存器重新计算全部窗口与 IRQ。 */
function update(ctx) {
  const state = ctx.state;
  ctx.nametable.set(MIRRORING[state.ctrl & 3]);
  if (initState === 0 && (state.chr0 & 0x10) === 0) initState = 1;
  else if (initState === 1 && (state.chr0 & 0x10) !== 0) initState = 2;
  if (state.chr0 & 0x10) {
    irqEnabled = false;
    ctx.timer.cancel('irq');
    ctx.irq.release();
  } else if (!irqEnabled) {
    irqEnabled = true;
    ctx.timer.after('irq', maxCounter(ctx));
  }
  const prg0 = ctx.window('prg-8000-16k');
  const prg1 = ctx.window('prg-c000-16k');
  const prgMode = (state.ctrl >> 3) & 1;
  const slotSelect = (state.ctrl >> 2) & 1;
  if (initState === 2) {
    if (state.chr0 & 0x08) {
      const prgReg = (state.prg & 0x07) | 0x08;
      if (prgMode) {
        if (slotSelect) {
          prg0.setBank(prgReg);
          prg1.setBank(0x0F);
        } else {
          prg0.setBank(0x08);
          prg1.setBank(prgReg);
        }
      } else {
        prg0.setBank(prgReg & 0xFE);
        prg1.setBank((prgReg & 0xFE) + 1);
      }
    } else {
      prg0.setBank(state.chr0 & 0x06);
      prg1.setBank((state.chr0 & 0x06) + 1);
    }
  } else {
    prg0.setBank(0);
    prg1.setBank(1);
  }
  ctx.window('chr-0000').setBank(0);
  ctx.window('chr-1000').setBank(1);
}

/** 串行寄存器写满 5 位后写入对应的寄存器。 */
function commit(ctx, addr, value) {
  const state = ctx.state;
  switch (addr & 0xE000) {
    case 0x8000: state.ctrl = value; break;
    case 0xA000: state.lastChr1 = 0; state.chr0 = value; break;
    case 0xC000: state.lastChr1 = 1; state.chr1 = value; break;
    case 0xE000: state.prg = value; break;
  }
}

export default {
  onTimer(ctx) {
    irqEnabled = false;
    ctx.irq.assert();
  },
  reset(ctx, kind) {
    irqEnabled = false;
    if (kind !== 'power') {
      update(ctx);
      return;
    }
    initState = 0;
    lastWriteCycle = -1000;
    const state = ctx.state;
    state.ctrl = 0x0C;
    state.chr0 = 0x10;
    update(ctx);
  },
  write(ctx, bus, addr, value) {
    const now = ctx.cycle();
    // 与上一次写入相距不到 2 个周期的写入（如读改写指令的第二次写）被忽略；复位位照常处理。
    const accepted = (value & 0x80) !== 0 || now - lastWriteCycle >= 2;
    lastWriteCycle = now;
    if (!accepted) return;
    const state = ctx.state;
    if (value & 0x80) {
      state.shiftReg = 0;
      state.shiftCount = 0;
      state.ctrl = state.ctrl | 0x0C;
    } else {
      state.shiftReg = (state.shiftReg >> 1) | ((value << 4) & 0x10);
      state.shiftCount = state.shiftCount + 1;
      if (state.shiftCount === 5) {
        commit(ctx, addr, state.shiftReg);
        state.shiftReg = 0;
        state.shiftCount = 0;
      }
    }
    update(ctx);
  }
};
