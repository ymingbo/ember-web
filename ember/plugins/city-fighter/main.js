/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：City Fighter的寄存器逻辑。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-17
 * 备注：计数器按 CPU 周期递减：写寄存器时算出下一次减到 0 的时刻，到点回调 onTimer 拉起 IRQ，不逐周期运行脚本。
 * ------------------------------------------------------------------------
 */

const PRG_IDS = ['prg-8000', 'prg-a000', 'prg-c000', 'prg-e000'];
const CHR_IDS = ['chr-0000', 'chr-0400', 'chr-0800', 'chr-0c00', 'chr-1000', 'chr-1400', 'chr-1800', 'chr-1c00'];
const chr = new Array(8).fill(0);
let prgReg = 0;
let prgMode = 0;
let mirroring = 0;
const irq = { value: 0, enabled: false, start: 0 };

function liveCounter(ctx) {
  return irq.enabled ? (irq.value - (ctx.cycle() - irq.start)) & 0xFFFF : irq.value;
}

function setCounter(ctx, value, enabled) {
  irq.value = value & 0xFFFF;
  irq.enabled = enabled;
  irq.start = ctx.cycle();
  ctx.timer.cancel('irq');
  if (enabled) ctx.timer.after('irq', irq.value === 0 ? 0x10000 : irq.value);
}

function update(ctx) {
  for (let index = 0; index < 4; index += 1) ctx.window(PRG_IDS[index]).setBank(prgReg + index);
  if (!prgMode) ctx.window('prg-c000').setBank(prgReg);
  CHR_IDS.forEach((id, index) => ctx.window(id).setBank(chr[index]));
  ctx.nametable.set(['vertical', 'horizontal', 'single0', 'single1'][mirroring]);
}

const CHR_SLOT = { 0xD: 0, 0xA: 2, 0xB: 4, 0xE: 6 };

export default {
  reset(ctx) {
    prgReg = 0;
    prgMode = 0;
    mirroring = 0;
    chr.fill(0);
    setCounter(ctx, 0, false);
    update(ctx);
  },
  write(ctx, bus, addr, value) {
    const register = addr & 0xF00C;
    const group = register >> 12;
    if (register === 0x9000) {
      prgReg = value & 0x0C;
      mirroring = value & 0x03;
    } else if (register === 0x9004) {
      prgReg = value & 0x0C;
    } else if (group === 0xC) {
      prgMode = value & 0x01;
    } else if (group in CHR_SLOT) {
      const slot = CHR_SLOT[group] + ((register & 0x08) !== 0 ? 1 : 0);
      if ((register & 0x04) === 0) chr[slot] = (chr[slot] & 0xF0) | (value & 0x0F);
      else chr[slot] = (chr[slot] & 0x0F) | ((value << 4) & 0xF0);
    } else if (register === 0xF000) {
      setCounter(ctx, (liveCounter(ctx) & 0x1E0) | ((value & 0x0F) << 1), irq.enabled);
    } else if (register === 0xF004) {
      setCounter(ctx, (liveCounter(ctx) & 0x1E) | ((value & 0x0F) << 5), irq.enabled);
    } else if (register === 0xF008) {
      setCounter(ctx, liveCounter(ctx), (value & 0x02) !== 0);
      ctx.irq.release();
    }
    update(ctx);
  },
  onTimer(ctx) {
    ctx.irq.assert();
    setCounter(ctx, 0, true);
  }
};
