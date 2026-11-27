/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：BMC 8157的寄存器逻辑。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-17
 * 备注：寄存器只记录最后一次写入的地址，每次都按这个地址重新计算各窗口。
 * ------------------------------------------------------------------------
 */

let lastAddr = 0;

function update(ctx) {
  const innerPrg0 = (lastAddr >> 2) & 0x07;
  const innerPrg1 = ((lastAddr >> 7) & 0x01) | ((lastAddr >> 8) & 0x02);
  const outer128 = (lastAddr >> 5) & 0x03;
  const outer512 = (lastAddr >> 8) & 0x01;
  let baseBank;
  if (innerPrg1 === 0) baseBank = 0;
  else if (innerPrg1 === 1) baseBank = innerPrg0;
  else baseBank = 7;
  const low = ctx.window('prg-8000-16k');
  const high = ctx.window('prg-c000-16k');
  if (outer512 && ctx.header.prgSize <= 1024 * 512 && parseInt(ctx.settings.get('dip'), 10) !== 0) {
    low.unmap();
    high.unmap();
  } else {
    low.setBank((outer512 << 6) | (outer128 << 3) | innerPrg0);
    high.setBank((outer512 << 6) | (outer128 << 3) | baseBank);
    ctx.nametable.set(lastAddr & 0x02 ? 'horizontal' : 'vertical');
  }
}

export default {
  reset(ctx) {
    lastAddr = 0;
    update(ctx);
  },
  write(ctx, bus, addr) {
    lastAddr = addr;
    update(ctx);
  }
};
