/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：BMC-63的寄存器逻辑。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-17
 * 备注：寄存器写入后统一重新计算各窗口，与状态保持一致。
 * ------------------------------------------------------------------------
 */

// 按写入地址摆放四个 8K 窗口；地址位 8-9 都为 1 时 $8000-$BFFF 不映射。
function write(ctx, bus, addr) {
  const group = (addr >> 1) & 0x1FC;
  const low = (addr >> 1) & 0x02;
  if ((addr & 0x0300) === 0x0300) {
    ctx.window('prg-8000').unmap();
    ctx.window('prg-a000').unmap();
  } else {
    ctx.window('prg-8000').setBank(group | ((addr & 0x02) !== 0 ? 0 : low));
    ctx.window('prg-a000').setBank(group | ((addr & 0x02) !== 0 ? 1 : (low | 1)));
  }
  ctx.window('prg-c000').setBank(group | ((addr & 0x02) !== 0 ? 2 : low));
  if ((addr & 0x0800) !== 0) ctx.window('prg-e000').setBank((addr & 0x7C) | ((addr & 0x06) !== 0 ? 0x03 : 0x01));
  else ctx.window('prg-e000').setBank(group | ((addr & 0x02) !== 0 ? 3 : (low | 1)));
  ctx.nametable.set((addr & 0x01) !== 0 ? 'horizontal' : 'vertical');
}

export default {
  reset(ctx, kind) {
    if (kind === 'power') write(ctx, 'cpu', 0x8000);
  },
  write
};
