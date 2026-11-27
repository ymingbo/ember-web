/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：Mapper 246的寄存器逻辑。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-17
 * 备注：寄存器写入后统一重新计算各窗口，与状态保持一致。
 * ------------------------------------------------------------------------
 */

const PRG = ['prg-8000', 'prg-a000', 'prg-c000', 'prg-e000'];
const CHR = ['chr-0000', 'chr-0800', 'chr-1000', 'chr-1800'];

export default {
  reset(ctx) {
    ctx.window('prg-e000').setBank(0xFF);
  },
  write(ctx, bus, addr, value) {
    if ((addr & 0x07) <= 0x03) ctx.window(PRG[addr & 0x03]).setBank(value);
    else ctx.window(CHR[addr & 0x03]).setBank(value);
  }
};
