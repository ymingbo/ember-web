/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：Magic Kid GooGoo的寄存器逻辑。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-17
 * 备注：寄存器写入后统一重新计算各窗口，与状态保持一致。
 * ------------------------------------------------------------------------
 */

const CHR = ['chr-0000', 'chr-0800', 'chr-1000', 'chr-1800'];

export default {
  write(ctx, bus, addr, value) {
    if (addr >= 0x8000 && addr <= 0x9FFF) ctx.window('prg-8000-16k').setBank(value & 0x07);
    else if (addr >= 0xC000 && addr <= 0xDFFF) ctx.window('prg-8000-16k').setBank((value & 0x07) | 0x08);
    else if ((addr & 0xA000) === 0xA000) ctx.window(CHR[addr & 0x03]).setBank(value);
  }
};
