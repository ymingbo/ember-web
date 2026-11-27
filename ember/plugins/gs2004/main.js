/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：GS-2004的寄存器逻辑。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-17
 * 备注：寄存器写入后统一重新计算各窗口，与状态保持一致。
 * ------------------------------------------------------------------------
 */

const PRG = ["prg-8000","prg-a000","prg-c000","prg-e000"];

function select(ctx, block) {
  for (let slot = 0; slot < 4; slot += 1) ctx.window(PRG[slot]).setBank((block << 2) + slot);
}

export default {
  reset(ctx) {
    ctx.window('prg-6000').setBank(0x20);
    select(ctx, 7);
  },
  write(ctx, bus, addr, value) {
    select(ctx, value & 0x07);
  }
};
