/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：Action Enterprises的寄存器逻辑。
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

function write(ctx, bus, addr, value) {
  let chip = (addr >> 11) & 0x03;
  if (chip === 3) chip = 2;
  const page = ((addr >> 6) & 0x1F) | (chip << 5);
  if ((addr & 0x20) !== 0) set16(ctx, page, page);
  else set16(ctx, page & 0xFE, (page & 0xFE) + 1);
  ctx.window('chr-0000').setBank(((addr & 0x0F) << 2) | (value & 0x03));
  ctx.nametable.set((addr & 0x2000) !== 0 ? 'horizontal' : 'vertical');
}

export default {
  reset(ctx) {
    write(ctx, 'cpu', 0x8000, 0);
  },
  write
};
