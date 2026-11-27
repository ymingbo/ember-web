/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：Mapper42的寄存器逻辑。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-17
 * 备注：计数器按 CPU 周期在宿主调度器里计时：写寄存器时算出下一次溢出的时刻，到点回调 onTimer 拉起 IRQ，不逐周期运行脚本。
 * ------------------------------------------------------------------------
 */

export default {
  write(ctx, bus, addr, value) {
    switch (addr & 0xE003) {
      case 0x8000:
        if (ctx.header.chrSize > 0) ctx.window('chr-0000').setBank(value & 0x0F);
        break;
      case 0xE000:
        ctx.window('prg-6000').setBank(value & 0x0F);
        break;
      case 0xE001:
        ctx.nametable.set(value & 0x08 ? 'horizontal' : 'vertical');
        break;
      case 0xE002:
        ctx.timer.cancel('on');
        ctx.timer.cancel('off');
        ctx.irq.release();
        if (value === 0x02) ctx.timer.after('on', 0x6000);
        break;
    }
  },
  onTimer(ctx, name) {
    if (name === 'on') {
      ctx.irq.assert();
      ctx.timer.after('off', 0x2000);
    } else {
      ctx.irq.release();
      ctx.timer.after('on', 0x6000);
    }
  }
};
