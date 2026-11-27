/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：Mapper 15的寄存器逻辑。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-17
 * 备注：寄存器写入后统一重新计算各窗口，与状态保持一致。
 * ------------------------------------------------------------------------
 */

const PRG = ["prg-8000","prg-a000","prg-c000","prg-e000"];

function write(ctx, bus, addr, value) {
  ctx.nametable.set((value & 0x40) !== 0 ? 'horizontal' : 'vertical');
  const sub = value >> 7;
  let bank = (value & 0x7F) << 1;
  const mode = addr & 0x03;
  ctx.window('chr-0000').setAccess(mode === 0 || mode === 3 ? 'r' : 'rw');
  const set = (slot, page) => ctx.window(PRG[slot]).setBank(page);
  switch (mode) {
    case 0:
      for (let slot = 0; slot < 4; slot += 1) set(slot, (bank + slot) ^ sub);
      break;
    case 1:
    case 3:
      bank |= sub;
      set(0, bank);
      set(1, bank + 1);
      bank = (mode === 3 ? bank : bank | 0x0E) | sub;
      set(2, bank);
      set(3, bank + 1);
      break;
    case 2:
      bank |= sub;
      for (let slot = 0; slot < 4; slot += 1) set(slot, bank);
      break;
  }
}

export default {
  reset(ctx) {
    write(ctx, 'cpu', 0x8000, 0);
  },
  write
};
