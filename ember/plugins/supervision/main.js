/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：Supervision 16-in-1的寄存器逻辑。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-17
 * 备注：不区分特定 ROM 的校验值（只按外层寄存器选页）。
 * ------------------------------------------------------------------------
 */

const PRG_IDS = ['prg-8000', 'prg-a000', 'prg-c000', 'prg-e000'];
const regs = [0, 0];

function update(ctx) {
  const outer = (regs[0] << 3) & 0x78;
  ctx.window('prg-6000').setBank((outer << 1) | 0x0F);
  let pages;
  if (regs[0] & 0x10) {
    const low = (outer | (regs[1] & 0x07)) << 1;
    const high = (outer | 0x07) << 1;
    pages = [low, low + 1, high, high + 1];
  } else {
    pages = [0x100, 0x101, 0x100, 0x101];
  }
  pages.forEach((page, index) => ctx.window(PRG_IDS[index]).setBank(page));
  ctx.nametable.set(regs[0] & 0x20 ? 'horizontal' : 'vertical');
}

export default {
  reset(ctx) {
    regs[0] = 0;
    regs[1] = 0;
    update(ctx);
  },
  write(ctx, bus, addr, value) {
    if (addr < 0x8000) regs[0] = value;
    else regs[1] = value;
    update(ctx);
  }
};
