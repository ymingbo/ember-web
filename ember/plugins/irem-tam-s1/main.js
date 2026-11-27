/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：Irem TAM-S1的寄存器逻辑。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-17
 * 备注：寄存器写入后统一重新计算各窗口，与状态保持一致。
 * ------------------------------------------------------------------------
 */

const MIRRORING = ['single0', 'horizontal', 'vertical', 'single1'];

export default {
  write(ctx, bus, addr, value) {
    ctx.window('prg-c000-16k').setBank(value & 0x0F);
    ctx.nametable.set(MIRRORING[value >> 6]);
  }
};
