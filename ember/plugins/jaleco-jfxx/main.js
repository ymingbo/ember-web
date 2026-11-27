/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：Jaleco JF-xx的寄存器逻辑。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-17
 * 备注：寄存器写入后统一重新计算各窗口，与状态保持一致。
 * ------------------------------------------------------------------------
 */

export default {
  write(ctx, bus, addr, value) {
    const bank = ctx.header.mapper === 101 ? value : ((value & 0x01) << 1) | ((value & 0x02) >> 1);
    ctx.window('chr-0000').setBank(bank);
  }
};
