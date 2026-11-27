/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：Kaiser KS-7037的寄存器逻辑。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-17
 * 备注：寄存器写入后统一重新计算各窗口，与状态保持一致。
 * ------------------------------------------------------------------------
 */

const regs = [0, 0, 0, 0, 0, 0, 0, 0];
let current = 0;

function update(ctx) {
  ctx.window('p-8000').setBank(regs[6] << 1);
  ctx.window('p-9000').setBank((regs[6] << 1) + 1);
  ctx.window('p-c000').setBank(regs[7] << 1);
  ctx.window('p-d000').setBank((regs[7] << 1) + 1);
  [2, 4, 3, 5].forEach((register, slot) => ctx.nametable.map(slot, regs[register] & 1 ? 'ciram1' : 'ciram0'));
}

export default {
  reset(ctx) {
    regs.fill(0);
    current = 0;
    update(ctx);
  },
  write(ctx, bus, addr, value) {
    switch (addr & 0xE001) {
      case 0x8000: current = value & 0x07; break;
      case 0x8001:
        regs[current] = value;
        update(ctx);
        break;
    }
  }
};
