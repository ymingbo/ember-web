/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：Henggedianzi 179的寄存器逻辑。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-17
 * 备注：寄存器写入后统一重新计算各窗口，与状态保持一致。
 * ------------------------------------------------------------------------
 */

export default {
  write(ctx, bus, addr, value) {
    if (addr >= 0x8000) ctx.nametable.set((value & 0x01) !== 0 ? 'horizontal' : 'vertical');
    else ctx.window('prg-8000-32k').setBank(value >> 1);
  }
};
