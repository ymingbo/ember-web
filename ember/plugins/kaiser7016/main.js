/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：Kaiser KS-7016的寄存器逻辑。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-17
 * 备注：寄存器写入后统一重新计算各窗口，与状态保持一致。
 * ------------------------------------------------------------------------
 */

export default {
  write(ctx, bus, addr) {
    const mode = (addr & 0x30) === 0x30;
    let bank = -1;
    switch (addr & 0xD943) {
      case 0xD943:
        bank = mode ? 0x0B : (addr >> 2) & 0x0F;
        break;
      case 0xD903:
        bank = mode ? 0x08 | ((addr >> 2) & 0x03) : 0x0B;
        break;
    }
    if (bank >= 0) ctx.window('prg-6000').setBank(bank);
  }
};
