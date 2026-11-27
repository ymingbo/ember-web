/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：Jaleco SS88006的寄存器逻辑。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-17
 * 备注：计数器按 CPU 周期在宿主调度器里计时：写寄存器时算出下一次溢出的时刻，到点回调 onTimer 拉起 IRQ，不逐周期运行脚本。
 * ------------------------------------------------------------------------
 */

const MASKS = [0xFFFF, 0x0FFF, 0x00FF, 0x000F];
const prg = [0, 0, 0];
const chr = [0, 0, 0, 0, 0, 0, 0, 0];
const reloadNibbles = [0, 0, 0, 0];
const PRG_IDS = ['prg-8000', 'prg-a000', 'prg-c000'];
const CHR_IDS = ['chr-0000', 'chr-0400', 'chr-0800', 'chr-0c00', 'chr-1000', 'chr-1400', 'chr-1800', 'chr-1c00'];
let size = 0;
let enabled = false;
let base = 0;
let start = 0;

function live(ctx) {
  const mask = MASKS[size];
  const low = ((base & mask) - (enabled ? ctx.cycle() - start : 0)) & mask;
  return (base & ~mask & 0xFFFF) | low;
}

function reschedule(ctx) {
  ctx.timer.cancel('irq');
  if (!enabled) return;
  const mask = MASKS[size];
  const low = live(ctx) & mask;
  ctx.timer.after('irq', low === 0 ? mask + 1 : low);
}

export default {
  reset() {
    prg.fill(0);
    chr.fill(0);
    reloadNibbles.fill(0);
    size = 0;
    enabled = false;
    base = 0;
    start = 0;
  },
  write(ctx, bus, addr, value) {
    const upper = (addr & 0x01) === 1;
    const nibble = value & 0x0F;
    const setPrg = (index) => {
      prg[index] = upper ? (prg[index] & 0x0F) | (nibble << 4) : (prg[index] & 0xF0) | nibble;
      ctx.window(PRG_IDS[index]).setBank(prg[index]);
    };
    const setChr = (index) => {
      chr[index] = upper ? (chr[index] & 0x0F) | (nibble << 4) : (chr[index] & 0xF0) | nibble;
      ctx.window(CHR_IDS[index]).setBank(chr[index]);
    };
    switch (addr & 0xF003) {
      case 0x8000: case 0x8001: setPrg(0); break;
      case 0x8002: case 0x8003: setPrg(1); break;
      case 0x9000: case 0x9001: setPrg(2); break;
      case 0xA000: case 0xA001: setChr(0); break;
      case 0xA002: case 0xA003: setChr(1); break;
      case 0xB000: case 0xB001: setChr(2); break;
      case 0xB002: case 0xB003: setChr(3); break;
      case 0xC000: case 0xC001: setChr(4); break;
      case 0xC002: case 0xC003: setChr(5); break;
      case 0xD000: case 0xD001: setChr(6); break;
      case 0xD002: case 0xD003: setChr(7); break;
      case 0xE000: case 0xE001: case 0xE002: case 0xE003: reloadNibbles[addr & 0x03] = nibble; break;
      case 0xF000:
        ctx.irq.release();
        base = reloadNibbles[0] | (reloadNibbles[1] << 4) | (reloadNibbles[2] << 8) | (reloadNibbles[3] << 12);
        start = ctx.cycle();
        reschedule(ctx);
        break;
      case 0xF001:
        ctx.irq.release();
        base = live(ctx);
        enabled = (nibble & 0x01) !== 0;
        size = nibble & 0x08 ? 3 : nibble & 0x04 ? 2 : nibble & 0x02 ? 1 : 0;
        start = ctx.cycle();
        reschedule(ctx);
        break;
      case 0xF002: ctx.nametable.set(['horizontal', 'vertical', 'single0', 'single1'][nibble & 0x03]); break;
    }
  },
  onTimer(ctx) {
    base = live(ctx);
    start = ctx.cycle();
    ctx.irq.assert();
    reschedule(ctx);
  }
};
