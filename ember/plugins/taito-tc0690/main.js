/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：Taito TC0690的寄存器逻辑。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-17
 * 备注：IRQ 计数按 A12 上升沿（宿主调用 onA12）；计数到 0 后用计时器延迟若干 CPU 周期再拉起 IRQ。
 * ------------------------------------------------------------------------
 */

const CHR_IDS = ['chr-0000', 'chr-0400', 'chr-0800', 'chr-0c00', 'chr-1000', 'chr-1400', 'chr-1800', 'chr-1c00'];
const irq = { enabled: false, reload: false, counter: 0, reloadValue: 0 };

function clockCounter(ctx) {
  if (irq.counter === 0 || irq.reload) irq.counter = irq.reloadValue;
  else irq.counter -= 1;
  if (irq.counter === 0 && irq.enabled) ctx.timer.after('delay', ctx.header.variant === 1 ? 6 : 22);
  irq.reload = false;
}

export default {
  reset(ctx) {
    irq.enabled = false;
    irq.reload = false;
    irq.counter = 0;
    irq.reloadValue = 0;
  },
  write(ctx, bus, addr, value) {
    switch (addr & 0xE003) {
      case 0x8000:
        ctx.window('prg-8000').setBank(value & 0x3F);
        break;
      case 0x8001:
        ctx.window('prg-a000').setBank(value & 0x3F);
        break;
      case 0x8002:
        ctx.window(CHR_IDS[0]).setBank(value * 2);
        ctx.window(CHR_IDS[1]).setBank(value * 2 + 1);
        break;
      case 0x8003:
        ctx.window(CHR_IDS[2]).setBank(value * 2);
        ctx.window(CHR_IDS[3]).setBank(value * 2 + 1);
        break;
      case 0xA000: case 0xA001: case 0xA002: case 0xA003:
        ctx.window(CHR_IDS[4 + (addr & 0x03)]).setBank(value);
        break;
      case 0xC000:
        ctx.irq.release();
        irq.reloadValue = ((value ^ 0xFF) + (ctx.header.variant === 1 ? 1 : 0)) & 0xFF;
        break;
      case 0xC001:
        ctx.irq.release();
        irq.counter = 0;
        irq.reload = true;
        break;
      case 0xC002:
        irq.enabled = true;
        break;
      case 0xC003:
        irq.enabled = false;
        ctx.irq.release();
        break;
      case 0xE000:
        ctx.nametable.set((value & 0x40) !== 0 ? 'horizontal' : 'vertical');
        break;
    }
  },
  onA12(ctx) {
    clockCounter(ctx);
  },
  onTimer(ctx) {
    ctx.irq.assert();
  }
};
