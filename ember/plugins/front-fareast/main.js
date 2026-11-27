/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：Front Fareast的寄存器逻辑。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-17
 * 备注：16 位计数器每个 CPU 周期加 1，回到 0 时触发 IRQ 并关闭：写寄存器时算出溢出的时刻，到点回调 onTimer，不逐周期运行脚本。
 * ------------------------------------------------------------------------
 */

const PRG_IDS = ['prg-8000', 'prg-a000', 'prg-c000', 'prg-e000'];
const CHR_IDS = ['chr-0000', 'chr-0400', 'chr-0800', 'chr-0c00', 'chr-1000', 'chr-1400', 'chr-1800', 'chr-1c00'];
const irq = { value: 0, enabled: false, start: 0 };
let altMode = true;

function liveCounter(ctx) {
  return irq.enabled ? (irq.value + (ctx.cycle() - irq.start)) & 0xFFFF : irq.value;
}

function setCounter(ctx, value, enabled) {
  irq.value = value & 0xFFFF;
  irq.enabled = enabled;
  irq.start = ctx.cycle();
  ctx.timer.cancel('irq');
  if (enabled) ctx.timer.after('irq', irq.value === 0 ? 0x10000 : 0x10000 - irq.value);
}

function prg(ctx, slot, page) {
  const window = ctx.window(PRG_IDS[slot]);
  window.setBank(page < 0 ? window.bankCount + page : page);
}

function chr8(ctx, base) {
  CHR_IDS.forEach((id, index) => ctx.window(id).setBank(base + index));
}

export default {
  reset(ctx) {
    altMode = true;
    setCounter(ctx, 0, false);
    switch (ctx.header.mapper) {
      case 6:
        prg(ctx, 0, 0); prg(ctx, 1, 1); prg(ctx, 2, 14); prg(ctx, 3, 15);
        break;
      case 8:
        for (let slot = 0; slot < 4; slot += 1) prg(ctx, slot, slot);
        break;
      default:
        for (let slot = 0; slot < 4; slot += 1) prg(ctx, slot, slot - 4);
        break;
    }
  },
  write(ctx, bus, addr, value) {
    const mapper = ctx.header.mapper;
    if (addr === 0x42FE) {
      altMode = (value & 0x80) === 0;
      ctx.nametable.set((value >> 4) & 0x01 ? 'single1' : 'single0');
    } else if (addr === 0x42FF) {
      ctx.nametable.set((value >> 4) & 0x01 ? 'horizontal' : 'vertical');
    } else if (addr === 0x4501) {
      setCounter(ctx, liveCounter(ctx), false);
      ctx.irq.release();
    } else if (addr === 0x4502) {
      setCounter(ctx, (liveCounter(ctx) & 0xFF00) | value, irq.enabled);
      ctx.irq.release();
    } else if (addr === 0x4503) {
      setCounter(ctx, (liveCounter(ctx) & 0x00FF) | (value << 8), true);
      ctx.irq.release();
    } else if (mapper === 6) {
      if (addr >= 0x8000) {
        if (ctx.header.chrSize === 0 || altMode) {
          const page = (value & 0xFC) >> 1;
          prg(ctx, 0, page);
          prg(ctx, 1, page + 1);
          value &= 0x03;
        }
        chr8(ctx, value << 3);
      }
    } else if (mapper === 8) {
      if (addr >= 0x8000) {
        const page = (value & 0xF8) >> 2;
        prg(ctx, 0, page);
        prg(ctx, 1, page + 1);
        chr8(ctx, (value & 0x07) << 3);
      }
    } else if (addr >= 0x4504 && addr <= 0x4507) {
      prg(ctx, addr - 0x4504, value);
    } else if (addr >= 0x4510 && addr <= 0x4517) {
      ctx.window(CHR_IDS[addr - 0x4510]).setBank(value);
    }
  },
  onTimer(ctx) {
    irq.enabled = false;
    irq.value = 0;
    ctx.irq.assert();
  }
};
