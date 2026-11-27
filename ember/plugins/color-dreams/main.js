/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：Color Dreams的寄存器逻辑。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-17
 * 备注：寄存器写入后统一重新计算各窗口，与状态保持一致。
 * ------------------------------------------------------------------------
 */

export default {
  write(ctx, bus, addr, value) {
    // mapper 144：总线冲突之后，ROM 字节的最低位总能胜出。
    if (ctx.header.mapper === 144) value |= (ctx.peek(addr) ?? 0) & 0x01;
    ctx.state.register = value;
    ctx.window('prg-8000-32k').setBank(value & 0x0F);
    ctx.window('chr-0000').setBank((value >> 4) & 0x0F);
  }
};
