/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：Henggedianzi 177的寄存器逻辑。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-17
 * 备注：寄存器写入后统一重新计算各窗口，与状态保持一致。
 * ------------------------------------------------------------------------
 */

export default {
  write(ctx, bus, addr, value) {
    ctx.window('prg-8000-32k').setBank(value);
    ctx.nametable.set((value & 0x20) !== 0 ? 'horizontal' : 'vertical');
  }
};
