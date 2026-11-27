/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：BS-5的寄存器逻辑。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-17
 * 备注：寄存器按地址而不是数据译码；窗口初始都指向最后一页。
 * ------------------------------------------------------------------------
 */

const PRG_IDS = ['prg-8000', 'prg-a000', 'prg-c000', 'prg-e000'];
const CHR_IDS = ['chr-0000', 'chr-0800', 'chr-1000', 'chr-1800'];

function last(window) {
  window.setBank(window.bankCount - 1);
}

export default {
  reset(ctx) {
    PRG_IDS.forEach((id) => last(ctx.window(id)));
    CHR_IDS.forEach((id) => last(ctx.window(id)));
  },
  write(ctx, bus, addr) {
    const slot = (addr >> 10) & 0x03;
    switch (addr & 0xF000) {
      case 0x8000:
        ctx.window(CHR_IDS[slot]).setBank(addr & 0x1F);
        break;
      case 0xA000:
        if (addr & (1 << ((parseInt(ctx.settings.get('dip'), 10) & 0x03) + 4))) ctx.window(PRG_IDS[slot]).setBank(addr & 0x0F);
        break;
    }
  }
};
