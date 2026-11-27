/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：Bandai 卡拉 OK的寄存器逻辑。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-17
 * 备注：寄存器写入后统一重新计算各窗口，与状态保持一致。
 * ------------------------------------------------------------------------
 */

export default {
  write(ctx, bus, addr, value) {
    const low = ctx.window('prg-8000-16k');
    if ((value & 0x10) !== 0) {
      low.setBank(value & 0x07);
    } else if (ctx.storage('prg').size >= 0x40000) {
      low.setBank((value & 0x07) | 0x08);
    } else {
      // 没有扩展 ROM 的卡带读开放总线。
      low.unmap();
    }
    ctx.nametable.set((value & 0x20) !== 0 ? 'horizontal' : 'vertical');
  }
};
