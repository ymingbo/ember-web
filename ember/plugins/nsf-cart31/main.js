/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：NSF 合集卡的寄存器逻辑。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-17
 * 备注：寄存器写入后统一重新计算各窗口，与状态保持一致。
 * ------------------------------------------------------------------------
 */

const WINDOWS = ["prg-8000","prg-9000","prg-a000","prg-b000","prg-c000","prg-d000","prg-e000","prg-f000"];

export default {
  reset(ctx, kind) {
    if (kind !== 'power') return;
    // 上电只映射最后一个窗口，bank 号 0xFF 按 bank 数取模。
    for (let slot = 0; slot < 7; slot += 1) ctx.window(WINDOWS[slot]).unmap();
    ctx.window(WINDOWS[7]).setBank(0xFF);
  },
  write(ctx, bus, addr, value) {
    ctx.window(WINDOWS[addr & 0x07]).setBank(value);
  }
};
