/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：Racermate的寄存器逻辑。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-17
 * 备注：计数器每个 CPU 周期递减，减到 0 时重载为 1024 并触发 IRQ：按周期在宿主调度器里计时，到点回调 onTimer。
 * ------------------------------------------------------------------------
 */

export default {
  reset(ctx) {
    // 上电计数器为 0，第一次减到 0 要等 65536 个周期。
    ctx.timer.cancel('irq');
    ctx.timer.after('irq', 0x10000);
  },
  write(ctx, bus, addr, value) {
    if ((addr & 0xC000) === 0x8000) {
      const window = ctx.window('prg-8000-16k');
      window.setBank((value >> 6) & 0x03);
      ctx.window('chr-1000').setBank(value & 0x0F);
    } else {
      ctx.irq.release();
      ctx.timer.cancel('irq');
      ctx.timer.after('irq', 1024);
    }
  },
  onTimer(ctx) {
    ctx.irq.assert();
    ctx.timer.after('irq', 1024);
  }
};
