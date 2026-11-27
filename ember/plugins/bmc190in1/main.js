/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：BMC-190in1的寄存器逻辑。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-17
 * 备注：寄存器写入后统一重新计算各窗口，与状态保持一致。
 * ------------------------------------------------------------------------
 */

export default {
  write(ctx, bus, addr, value) {
    const bank = (value >> 2) & 0x07;
    ctx.window('prg-8000-16k').setBank(bank);
    ctx.window('prg-c000-16k').setBank(bank);
    ctx.window('chr-0000').setBank(bank);
    ctx.nametable.set((value & 0x01) !== 0 ? 'horizontal' : 'vertical');
  }
};
