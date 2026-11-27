/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：Mapper40的寄存器逻辑。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-17
 * 备注：计数器按 CPU 周期在宿主调度器里计时：写寄存器时算出下一次溢出的时刻，到点回调 onTimer 拉起 IRQ，不逐周期运行脚本。
 * ------------------------------------------------------------------------
 */

export default {
  write(ctx, bus, addr, value) {
    switch (addr & 0xE000) {
      case 0x8000:
        ctx.timer.cancel('irq');
        ctx.irq.release();
        break;
      case 0xA000:
        ctx.timer.after('irq', 4096);
        break;
      case 0xE000:
        ctx.window('prg-c000').setBank(value);
        break;
    }
  },
  onTimer(ctx) {
    ctx.irq.assert();
  }
};
