/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：Mapper43的寄存器逻辑。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-17
 * 备注：计数器按 CPU 周期在宿主调度器里计时：写寄存器时算出下一次溢出的时刻，到点回调 onTimer 拉起 IRQ，不逐周期运行脚本。
 * ------------------------------------------------------------------------
 */

const LUT = [4, 3, 5, 3, 6, 3, 7, 3];
let reg = 0;
let swap = false;

function update(ctx) {
  ctx.window('prg-c000').setBank(reg);
  ctx.window('prg-6000').setBank(swap ? 0 : 2);
  ctx.window('prg-e000').setBank(swap ? 8 : 9);
}

export default {
  reset(ctx) {
    reg = 0;
    swap = false;
    update(ctx);
  },
  write(ctx, bus, addr, value) {
    switch (addr & 0xF1FF) {
      case 0x4022:
        reg = LUT[value & 0x07];
        update(ctx);
        break;
      case 0x4120:
        swap = (value & 0x01) !== 0;
        update(ctx);
        break;
      case 0x8122:
      case 0x4122:
        ctx.irq.release();
        ctx.timer.cancel('irq');
        if (value & 0x01) ctx.timer.after('irq', 4096);
        break;
    }
  },
  onTimer(ctx) {
    ctx.irq.assert();
  }
};
