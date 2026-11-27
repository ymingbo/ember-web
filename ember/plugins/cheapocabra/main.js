/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：Cheapocabra的寄存器逻辑。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-17
 * 备注：名称表显存是卡带自带的 16K，用逐个名称表映射；闪存的命令写入没有实现，所以不能存档。
 * ------------------------------------------------------------------------
 */

function apply(ctx, value) {
  ctx.window('prg-8000-32k').setBank(value & 0x0F);
  ctx.window('chr-0000').setBank((value >> 4) & 0x01);
  for (let slot = 0; slot < 4; slot += 1) ctx.nametable.map(slot, 'ntram', (value & 0x20 ? 8 : 0) + slot);
}

export default {
  reset(ctx, kind) {
    if (kind === 'power') apply(ctx, 0);
  },
  write(ctx, bus, addr, value) {
    apply(ctx, value);
  }
};
