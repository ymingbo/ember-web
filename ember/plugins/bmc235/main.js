/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：BMC-235的寄存器逻辑。
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

// 各 PRG 容量下按地址位 8-9 取 [外层 bank, 是否开放总线]。
const CONFIG = [
  [[0x00, 0], [0x00, 1], [0x00, 1], [0x00, 1]],
  [[0x00, 0], [0x00, 1], [0x20, 0], [0x00, 1]],
  [[0x00, 0], [0x00, 1], [0x20, 0], [0x40, 0]],
  [[0x00, 0], [0x20, 0], [0x40, 0], [0x60, 0]]
];

export default {
  reset(ctx) {
    set16(ctx, 0, 1);
  },
  write(ctx, bus, addr) {
    if ((addr & 0x0400) !== 0) ctx.nametable.set('single0');
    else ctx.nametable.set((addr & 0x2000) !== 0 ? 'horizontal' : 'vertical');
    const pages = ctx.storage('prg').size / 16384;
    const mode = pages === 64 ? 0 : pages === 128 ? 1 : pages === 256 ? 2 : 3;
    const entry = CONFIG[mode][(addr >> 8) & 0x03];
    let bank = entry[0] | (addr & 0x1F);
    if (entry[1] !== 0) {
      ctx.window('prg-8000-16k').unmap();
      ctx.window('prg-c000-16k').unmap();
    } else if ((addr & 0x800) !== 0) {
      bank = (bank << 1) | ((addr >> 12) & 0x01);
      set16(ctx, bank, bank);
    } else {
      set16(ctx, bank << 1, (bank << 1) + 1);
    }
  }
};
