/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：CNROM 保护的寄存器逻辑。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-17
 * 备注：寄存器写入后统一重新计算各窗口，与状态保持一致。
 * ------------------------------------------------------------------------
 */

function setEnabled(ctx, enabled) {
  if (enabled) ctx.window('chr-0000').select('chr', 0);
  else ctx.window('chr-0000').select('stub', 0);
}

export default {
  reset(ctx, kind) {
    if (kind === 'power') {
      const stub = ctx.storage('stub');
      for (let index = 0; index < 8192; index += 1) stub.write(index, 0x01);
    }
    setEnabled(ctx, true);
  },
  write(ctx, bus, addr, value) {
    const variant = ctx.header.variant;
    const enabled = (variant === 0 && (value & 0x0F) !== 0 && value !== 0x13) ||
      (variant === 4 && (value & 0x03) === 0) ||
      (variant === 5 && (value & 0x03) === 1) ||
      (variant === 6 && (value & 0x03) === 2) ||
      (variant === 7 && (value & 0x03) === 3);
    setEnabled(ctx, enabled);
  }
};
