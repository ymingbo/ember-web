/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：RAMBO-1的寄存器逻辑。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-17
 * 备注：IRQ 有两种计数：A12 上升沿（宿主调用 onA12）和每 4 个 CPU 周期一次（用计时器），计数到 0 后延迟 1 到 2 个 CPU 周期再拉起 IRQ。
 * ------------------------------------------------------------------------
 */

const regs = [0, 2, 4, 5, 6, 7, 0, 1, 8, 9, 0, 0, 0, 0, 0, 2];
const CHR_IDS = ['chr-0000', 'chr-0400', 'chr-0800', 'chr-0c00', 'chr-1000', 'chr-1400', 'chr-1800', 'chr-1c00'];
const PRG_IDS = ['prg-8000', 'prg-a000', 'prg-c000', 'prg-e000'];
const irq = { enabled: false, cycleMode: false, needReload: false, force: false, counter: 0, reload: 0 };
let r8000 = 0;
let current = 0;
let chrMode = 0;
let prgMode = 0;

function updateState(ctx) {
  current = r8000 & 0x0F;
  chrMode = (r8000 & 0x80) >> 7;
  prgMode = (r8000 & 0x40) >> 6;
  const prg = (slot, page) => {
    const window = ctx.window(PRG_IDS[slot]);
    window.setBank(page < 0 ? window.bankCount + page : page);
  };
  if (prgMode === 0) {
    prg(0, regs[6]); prg(1, regs[7]); prg(2, regs[15]); prg(3, -1);
  } else {
    prg(0, regs[15]); prg(1, regs[7]); prg(2, regs[6]); prg(3, -1);
  }
  const inversion = chrMode ? 4 : 0;
  const chr = (slot, page) => ctx.window(CHR_IDS[slot ^ inversion]).setBank(page);
  chr(0, regs[0]);
  chr(2, regs[1]);
  chr(4, regs[2]);
  chr(5, regs[3]);
  chr(6, regs[4]);
  chr(7, regs[5]);
  if (r8000 & 0x20) {
    chr(1, regs[8]);
    chr(3, regs[9]);
  } else {
    chr(1, regs[0] | 0x01);
    chr(3, regs[1] | 0x01);
  }
}

function clockCounter(ctx, delay) {
  if (irq.needReload) {
    irq.counter = (irq.reload <= 1 ? irq.reload + 1 : irq.reload + 2) & 0xFF;
    irq.needReload = false;
  } else if (irq.counter === 0) {
    irq.counter = (irq.reload + 1) & 0xFF;
  }
  irq.counter = (irq.counter - 1) & 0xFF;
  if (irq.counter === 0 && irq.enabled) ctx.timer.after('delay', delay);
}

export default {
  reset(ctx) {
    regs.splice(0, 16, 0, 2, 4, 5, 6, 7, 0, 1, 8, 9, 0, 0, 0, 0, 0, 2);
    r8000 = 0;
    irq.enabled = false;
    irq.cycleMode = false;
    irq.needReload = false;
    irq.force = false;
    irq.counter = 0;
    irq.reload = 0;
    ctx.irq.release();

    updateState(ctx);
  },
  write(ctx, bus, addr, value) {

    switch (addr & 0xE001) {
      case 0x8000:
        r8000 = value;
        updateState(ctx);
        break;
      case 0x8001:
        regs[current] = value;
        updateState(ctx);
        break;
      case 0xA000:
        if (!false) ctx.nametable.set(value & 0x01 ? 'horizontal' : 'vertical');
        break;
      case 0xC000:
        irq.reload = value;
        break;
      case 0xC001:
        if (irq.cycleMode && (value & 0x01) === 0) irq.force = true;
        irq.cycleMode = (value & 0x01) !== 0;
        if (irq.cycleMode) {
          ctx.timer.cancel('tick');
          ctx.timer.after('tick', 4);
        } else if (!irq.force) {
          ctx.timer.cancel('tick');
        }
        irq.needReload = true;
        break;
      case 0xE000:
        irq.enabled = false;
        ctx.irq.release();
        break;
      case 0xE001:
        irq.enabled = true;
        break;
    }
  },
  onA12(ctx) {
    if (!irq.cycleMode) clockCounter(ctx, 2);
  },
  onTimer(ctx, name) {
    if (name === 'delay') {
      ctx.irq.assert();
      return;
    }
    clockCounter(ctx, 1);
    irq.force = false;
    if (irq.cycleMode) ctx.timer.after('tick', 4);
  }
};
