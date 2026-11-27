/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：LH51的寄存器逻辑。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-17
 * 备注：寄存器写入后统一重新计算各窗口，与状态保持一致。
 * ------------------------------------------------------------------------
 */

export default {
  write(ctx, bus, addr, value) {
    switch (addr & 0xE000) {
      case 0x8000:
        ctx.window('prg-8000').setBank(value & 0x0F);
        break;
      case 0xE000:
        ctx.nametable.set((value & 0x08) !== 0 ? 'horizontal' : 'vertical');
        break;
    }
  }
};
