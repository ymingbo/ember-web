/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：UNL-D1038的寄存器逻辑。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-17
 * 备注：读 DIP 的状态用一块填满 DIP 值的 16K 存储区，让两个程序窗口切换过去，不必逐次回调读。
 * ------------------------------------------------------------------------
 */

export default {
  reset(ctx) {
    const value = parseInt(ctx.settings.get('dip'), 10) & 0x03;
    if (value !== 0) {
      const dip = ctx.storage('dip');
      for (let index = 0; index < 16384; index += 1) dip.write(index, value);
    }
    applyPage(ctx, 0x8000);
  },
  write(ctx, bus, addr) {
    applyPage(ctx, addr);
  }
};

function applyPage(ctx, addr) {
  const low = ctx.window('prg-8000-16k');
  const high = ctx.window('prg-c000-16k');
  if (addr & 0x100) {
    low.select('dip', 0);
    high.select('dip', 0);
  } else if (addr & 0x80) {
    const page = (addr & 0x70) >> 4;
    low.select('prg', page);
    high.select('prg', page);
  } else {
    const page = (addr & 0x60) >> 4;
    low.select('prg', page);
    high.select('prg', page + 1);
  }
  ctx.window('chr-0000').setBank(addr & 0x07);
  ctx.nametable.set(addr & 0x08 ? 'horizontal' : 'vertical');
}
