/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：Magic Floor的寄存器逻辑。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-17
 * 备注：寄存器写入后统一重新计算各窗口，与状态保持一致。
 * ------------------------------------------------------------------------
 */

export default {
  reset(ctx) {
    // 四屏的文件按文件头的第 0 位当作单屏 A，没有这个信息时取单屏 A。
    const mirroring = ctx.header.mirroring === 'four' ? 'single0' : ctx.header.mirroring;
    const mask = mirroring === 'vertical' ? 0x400 : mirroring === 'horizontal' ? 0x800 : mirroring === 'single1' ? 0x2000 : 0x1000;
    for (let index = 0; index < 8; index += 1) {
      ctx.window('chr-' + (index * 0x400).toString(16).padStart(4, '0')).setBank((index * 0x400) & mask ? 1 : 0);
    }
    for (let slot = 0; slot < 4; slot += 1) {
      const page = mirroring === 'vertical' ? slot & 1 : mirroring === 'horizontal' ? slot >> 1 : mirroring === 'single1' ? 1 : 0;
      ctx.nametable.map(slot, 'ntram', page);
    }
  }
};
