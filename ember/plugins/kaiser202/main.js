/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：Kaiser KS-202的寄存器逻辑。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-17
 * 备注：计数器按 CPU 周期在宿主调度器里计时：写寄存器时算出下一次溢出的时刻，到点回调 onTimer 拉起 IRQ，不逐周期运行脚本。
 * ------------------------------------------------------------------------
 */

const PRG_REGS = [0, 0, 0, 0];
let useRom = false;
let selected = 0;
let reload = 0;
let control = 0;
let counter = 0;
let start = 0;

function update(ctx) {
  const ram = ctx.window('prg-6000');
  if (useRom) {
    ram.select('prg', PRG_REGS[3]);
    ram.setAccess('r');
  } else {
    ram.select('prg-ram', 0);
    ram.setAccess('rw');
  }
  ctx.window('prg-8000').setBank(PRG_REGS[0]);
  ctx.window('prg-a000').setBank(PRG_REGS[1]);
  ctx.window('prg-c000').setBank(PRG_REGS[2]);
}

function reschedule(ctx) {
  ctx.timer.cancel('irq');
  if (control & 0x02) ctx.timer.after('irq', Math.max(1, 0xFFFF - counter));
}

export default {
  reset(ctx) {
    PRG_REGS.fill(0);
    useRom = false;
    selected = 0;
    reload = 0;
    control = 0;
    counter = 0;
    start = 0;
    update(ctx);
  },
  write(ctx, bus, addr, value) {
    switch (addr & 0xF000) {
      case 0x8000: reload = (reload & 0xFFF0) | (value & 0x0F); break;
      case 0x9000: reload = (reload & 0xFF0F) | ((value & 0x0F) << 4); break;
      case 0xA000: reload = (reload & 0xF0FF) | ((value & 0x0F) << 8); break;
      case 0xB000: reload = (reload & 0x0FFF) | ((value & 0x0F) << 12); break;
      case 0xC000:
        control = value;
        if (control & 0x02) counter = reload;
        start = ctx.cycle();
        ctx.irq.release();
        reschedule(ctx);
        break;
      case 0xD000:
        ctx.irq.release();
        break;
      case 0xE000:
        selected = ((value & 0x07) - 1) & 0xFF;
        break;
      case 0xF000:
        if (selected < 4) PRG_REGS[selected] = (PRG_REGS[selected] & 0x10) | (value & 0x0F);
        else if (selected === 4) useRom = (value & 0x04) !== 0;
        if (ctx.header.mapper === 56) {
          switch (addr & 0xFC00) {
            case 0xF000: {
              const index = addr & 0x03;
              PRG_REGS[index] = (value & 0x10) | (PRG_REGS[index] & 0x0F);
              break;
            }
            case 0xF800: ctx.nametable.set(value & 0x01 ? 'vertical' : 'horizontal'); break;
            case 0xFC00: ctx.window('chr-' + ((addr & 0x07) * 0x400).toString(16).padStart(4, '0')).setBank(value); break;
          }
        }
        update(ctx);
        break;
    }
  },
  onTimer(ctx) {
    counter = reload;
    control &= ~0x02;
    ctx.irq.assert();
  }
};
