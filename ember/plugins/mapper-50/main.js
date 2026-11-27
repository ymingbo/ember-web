/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：Mapper50的寄存器逻辑。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-17
 * 备注：计数器按 CPU 周期在宿主调度器里计时：写寄存器时算出下一次溢出的时刻，到点回调 onTimer 拉起 IRQ，不逐周期运行脚本。
 * ------------------------------------------------------------------------
 */

// 计数器从上次清零起算，到 $1000 时拉起 IRQ；拉起后不再计数，直到被写入关闭。
let enabled = false;
let counter = 0;

export default {
  reset() {
    enabled = false;
    counter = 0;
  },
  write(ctx, bus, addr, value) {
    switch (addr & 0x4120) {
      case 0x4020:
        ctx.window('prg-c000').setBank((value & 0x08) | ((value & 0x01) << 2) | ((value & 0x06) >> 1));
        break;
      case 0x4120:
        if (value & 0x01) {
          if (!enabled) {
            enabled = true;
            if (counter < 0x1000) ctx.timer.after('irq', 0x1000 - counter);
          }
        } else {
          enabled = false;
          counter = 0;
          ctx.timer.cancel('irq');
          ctx.irq.release();
        }
        break;
    }
  },
  onTimer(ctx) {
    ctx.irq.assert();
    enabled = false;
    counter = 0x1000;
  }
};
