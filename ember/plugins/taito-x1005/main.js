/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：Taito X1005的寄存器逻辑。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-17
 * 备注：寄存器写入后统一重新计算各窗口，与状态保持一致。
 * ------------------------------------------------------------------------
 */

/** 按文件头的镜像方式设置 4 个名称表槽（只有主板显存的两页可选）。 */
function headerMirroring(ctx) {
  const vertical = ctx.header.mirroring === 'vertical';
  [0, 1, 2, 3].forEach((slot) => ctx.nametable.map(slot, (vertical ? slot & 1 : slot >> 1) ? 'ciram1' : 'ciram0'));
}

function mirrorVertical(ctx) {
  [0, 1, 2, 3].forEach((slot) => ctx.nametable.map(slot, slot & 1 ? 'ciram1' : 'ciram0'));
}

function mirrorHorizontal(ctx) {
  [0, 1, 2, 3].forEach((slot) => ctx.nametable.map(slot, slot >> 1 ? 'ciram1' : 'ciram0'));
}

const CHR_IDS = ['chr-0000', 'chr-0400', 'chr-0800', 'chr-0c00', 'chr-1000', 'chr-1400', 'chr-1800', 'chr-1c00'];
let permission = 0;

function updateRam(ctx) {
  for (const id of ['ram-7f00', 'ram-7f80']) {
    if (permission === 0xA3) ctx.window(id).setBank(0);
    else ctx.window(id).unmap();
  }
}

function setChr(ctx, slot, page) {
  ctx.window(CHR_IDS[slot]).setBank(page & 0xFF);
}

export default {
  reset(ctx) {
    permission = 0;
    updateRam(ctx);
    headerMirroring(ctx);
  },
  write(ctx, bus, addr, value) {
    const alternate = ctx.header.mapper === 207;
    const source = value & 0x80 ? 'ciram1' : 'ciram0';
    switch (addr) {
      case 0x7EF0:
        setChr(ctx, 0, value);
        setChr(ctx, 1, value + 1);
        if (alternate) {
          ctx.nametable.map(0, source);
          ctx.nametable.map(1, source);
        }
        break;
      case 0x7EF1:
        setChr(ctx, 2, value);
        setChr(ctx, 3, value + 1);
        if (alternate) {
          ctx.nametable.map(2, source);
          ctx.nametable.map(3, source);
        }
        break;
      case 0x7EF2: setChr(ctx, 4, value); break;
      case 0x7EF3: setChr(ctx, 5, value); break;
      case 0x7EF4: setChr(ctx, 6, value); break;
      case 0x7EF5: setChr(ctx, 7, value); break;
      case 0x7EF6:
      case 0x7EF7:
        if (!alternate) {
          if (value & 0x01) mirrorVertical(ctx);
          else mirrorHorizontal(ctx);
        }
        break;
      case 0x7EF8:
      case 0x7EF9:
        permission = value;
        updateRam(ctx);
        break;
      case 0x7EFA:
      case 0x7EFB: ctx.window('prg-8000').setBank(value); break;
      case 0x7EFC:
      case 0x7EFD: ctx.window('prg-a000').setBank(value); break;
      case 0x7EFE:
      case 0x7EFF: ctx.window('prg-c000').setBank(value); break;
    }
  }
};
