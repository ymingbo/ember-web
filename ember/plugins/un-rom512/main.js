/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：UNROM 512的寄存器逻辑。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-17
 * 备注：总线冲突按子 mapper 决定（子 mapper 0 且不带电池、或子 mapper 2 时有）；闪存的命令写入没有实现，所以带电池的卡带只能读，不会保存。
 * ------------------------------------------------------------------------
 */

let enableMirroringBit = false;

export default {
  reset(ctx, kind) {
    const header = ctx.header;
    ctx.busConflict.set((header.variant === 0 && !header.battery) || header.variant === 2);
    if (kind !== 'power') return;
    enableMirroringBit = header.variant === 3;
    if (enableMirroringBit) ctx.nametable.set('vertical');
    ctx.window('prg-8000-16k').setBank(0);
    ctx.window('prg-c000-16k').setBank(ctx.window('prg-c000-16k').bankCount - 1);
    ctx.window('chr-0000').setBank(0);
  },
  write(ctx, bus, addr, value) {
    const header = ctx.header;
    if (header.battery && addr < 0xC000) return;
    if (header.variant === 4 && addr < 0xC000) return;
    ctx.window('prg-8000-16k').setBank(value & 0x1F);
    ctx.window('chr-0000').setBank((value >> 5) & 0x03);
    if (enableMirroringBit) ctx.nametable.set(value & 0x80 ? 'vertical' : 'horizontal');
  }
};
