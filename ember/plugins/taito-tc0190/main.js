/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：Taito TC0190的寄存器逻辑。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-17
 * 备注：寄存器写入后统一重新计算各窗口，与状态保持一致。
 * ------------------------------------------------------------------------
 */

const CHR = ["chr-0000","chr-0400","chr-0800","chr-0c00","chr-1000","chr-1400","chr-1800","chr-1c00"];

export default {
  write(ctx, bus, addr, value) {
    switch (addr & 0xA003) {
      case 0x8000:
        ctx.window('prg-8000').setBank(value & 0x3F);
        ctx.nametable.set((value & 0x40) !== 0 ? 'horizontal' : 'vertical');
        break;
      case 0x8001:
        ctx.window('prg-a000').setBank(value & 0x3F);
        break;
      case 0x8002:
        ctx.window(CHR[0]).setBank(value * 2);
        ctx.window(CHR[1]).setBank(value * 2 + 1);
        break;
      case 0x8003:
        ctx.window(CHR[2]).setBank(value * 2);
        ctx.window(CHR[3]).setBank(value * 2 + 1);
        break;
      case 0xA000:
      case 0xA001:
      case 0xA002:
      case 0xA003:
        ctx.window(CHR[4 + (addr & 0x03)]).setBank(value);
        break;
    }
  }
};
