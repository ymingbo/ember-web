/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：Sachen 74LS374N的寄存器逻辑。
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

const regs = [0, 0, 0, 0, 0, 0, 0, 0];
let current = 0;

function dip(ctx) {
  return ctx.header.mapper === 150 && ctx.settings.get('dip') === true;
}

function update(ctx) {
  const page = ctx.header.mapper === 150 ? ((regs[4] & 0x01) << 2) | (regs[6] & 0x03) : (regs[2] & 0x01) | ((regs[4] & 0x01) << 1) | ((regs[6] & 0x03) << 2);
  ctx.window('chr-0000').setBank(page);
  ctx.window('prg-8000-32k').setBank(regs[5] & 0x03);
  switch ((regs[7] >> 1) & 0x03) {
    case 0:
      [0, 1, 2].forEach((slot) => ctx.nametable.map(slot, 'ciram0'));
      ctx.nametable.map(3, 'ciram1');
      break;
    case 1: mirrorHorizontal(ctx); break;
    case 2: mirrorVertical(ctx); break;
    case 3: [0, 1, 2, 3].forEach((slot) => ctx.nametable.map(slot, 'ciram0')); break;
  }
}

export default {
  reset(ctx) {
    regs.fill(0);
    current = 0;
    update(ctx);
  },
  read(ctx, bus, addr) {
    if ((addr & 0xC101) !== 0x4101) return undefined;
    return dip(ctx) ? regs[current] & 0x03 : regs[current] & 0x07;
  },
  write(ctx, bus, addr, value) {
    const data = dip(ctx) ? value | 0x04 : value;
    switch (addr & 0xC101) {
      case 0x4100: current = data & 0x07; break;
      case 0x4101:
        regs[current] = data & 0x07;
        update(ctx);
        break;
    }
  }
};
