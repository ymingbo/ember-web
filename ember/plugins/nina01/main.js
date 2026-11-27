/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：NINA-001的寄存器逻辑。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-17
 * 备注：寄存器写入后统一重新计算各窗口，与状态保持一致。
 * ------------------------------------------------------------------------
 */

export default {
  write(ctx, bus, addr, value) {
    if (addr === 0x7FFD) ctx.window('prg-8000-32k').setBank(value);
    else if (addr === 0x7FFE) ctx.window('chr-0000').setBank(value);
    else ctx.window('chr-1000').setBank(value);
    // 这三个地址在寄存器之外也是 PRG RAM 的一部分，写入同时存进 RAM。
    const ram = ctx.storage('prg-ram');
    if (ram.size > 0) ram.write((addr - 0x6000) % ram.size, value);
  }
};
