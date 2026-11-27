/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：BMC-K-3046的寄存器逻辑。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-17
 * 备注：寄存器写入后统一重新计算各窗口，与状态保持一致。
 * ------------------------------------------------------------------------
 */

const set16 = (ctx, low, high) => {
  ctx.window('prg-8000-16k').setBank(low);
  ctx.window('prg-c000-16k').setBank(high);
};

export default {
  write(ctx, bus, addr, value) {
    const outer = value & 0x38;
    set16(ctx, outer | (value & 0x07), outer | 7);
  }
};
