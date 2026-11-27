/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：Sachen 8259的寄存器逻辑。
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
// 各型号：2K 页号的左移位数与三个或值（D 型是 1K 页，没有这些）。
const VARIANTS = { 141: { shift: 1, or: [1, 0, 1] }, 138: { shift: 0, or: [0, 0, 0] }, 139: { shift: 2, or: [1, 2, 3] }, 137: { shift: 0, or: [0, 0, 0] } };
const regs = [0, 0, 0, 0, 0, 0, 0, 0];
let current = 0;

function update(ctx) {
  const mapper = ctx.header.mapper;
  const dType = mapper === 137;
  const variant = VARIANTS[mapper];
  const simple = (regs[7] & 0x01) !== 0;
  switch ((regs[7] >> 1) & 0x03) {
    case 0: if (dType) mirrorHorizontal(ctx); else mirrorVertical(ctx); break;
    case 1: if (dType) mirrorVertical(ctx); else mirrorHorizontal(ctx); break;
    case 2:
      ctx.nametable.map(0, 'ciram0');
      [1, 2, 3].forEach((slot) => ctx.nametable.map(slot, 'ciram1'));
      break;
    case 3: [0, 1, 2, 3].forEach((slot) => ctx.nametable.map(slot, 'ciram0')); break;
  }
  if (simple) {
    if (dType) mirrorHorizontal(ctx);
    else mirrorVertical(ctx);
  }
  ctx.window('prg-8000-32k').setBank(regs[5]);
  const pick = (index) => regs[simple ? 0 : index];
  if (dType) {
    const pages = [regs[0], ((regs[4] & 0x01) << 4) | pick(1), ((regs[4] & 0x02) << 3) | pick(2), ((regs[4] & 0x04) << 2) | ((regs[6] & 0x01) << 3) | pick(3)];
    pages.forEach((page, slot) => ctx.window(CHR_IDS[slot]).setBank(page));
    for (let slot = 4; slot < 8; slot += 1) ctx.window(CHR_IDS[slot]).setBank(ctx.window(CHR_IDS[slot]).bankCount - 8 + slot);
  } else if (ctx.header.chrSize > 0) {
    const high = regs[4] << 3;
    const pages = [(high | regs[0]) << variant.shift, ((high | pick(1)) << variant.shift) | variant.or[0], ((high | pick(2)) << variant.shift) | variant.or[1], ((high | pick(3)) << variant.shift) | variant.or[2]];
    pages.forEach((page, slot) => {
      ctx.window(CHR_IDS[slot * 2]).setBank(page * 2);
      ctx.window(CHR_IDS[slot * 2 + 1]).setBank(page * 2 + 1);
    });
  }
}

export default {
  reset(ctx) {
    regs.fill(0);
    current = 0;
    update(ctx);
  },
  write(ctx, bus, addr, value) {
    switch (addr & 0xC101) {
      case 0x4100: current = value & 0x07; break;
      case 0x4101:
        regs[current] = value & 0x07;
        update(ctx);
        break;
    }
  }
};
