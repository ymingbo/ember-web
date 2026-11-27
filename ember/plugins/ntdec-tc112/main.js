/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：NTDEC TC-112的寄存器逻辑。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-17
 * 备注：寄存器写入后统一重新计算各窗口，与状态保持一致。
 * ------------------------------------------------------------------------
 */

export default {
  write(ctx, bus, addr, value) {
    switch (addr & 0x03) {
      case 0:
        ctx.window('chr-0000').setBank(value >> 1);
        ctx.window('chr-0800').setBank((value >> 1) + 1);
        break;
      case 1:
        ctx.window('chr-1000').setBank(value >> 1);
        break;
      case 2:
        ctx.window('chr-1800').setBank(value >> 1);
        break;
      case 3:
        ctx.window('prg-8000').setBank(value);
        break;
    }
  }
};
