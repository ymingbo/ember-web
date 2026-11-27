/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：Konami VRC1的寄存器逻辑。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-17
 * 备注：寄存器写入后统一重新计算各窗口，与状态保持一致。
 * ------------------------------------------------------------------------
 */

function updateChr(ctx) {
  ctx.window('chr-0000').setBank(ctx.state.chrBank0);
  ctx.window('chr-1000').setBank(ctx.state.chrBank1);
}

export default {
  write(ctx, bus, addr, value) {
    const state = ctx.state;
    switch (addr & 0xF000) {
      case 0x8000:
        ctx.window('prg-8000').setBank(value);
        break;
      case 0x9000:
        // 四屏接线时名称表由板子固定，忽略镜像位。
        if (ctx.header.mirroring !== 'four') ctx.nametable.set((value & 0x01) !== 0 ? 'horizontal' : 'vertical');
        state.chrBank0 = (state.chrBank0 & 0x0F) | ((value & 0x02) << 3);
        state.chrBank1 = (state.chrBank1 & 0x0F) | ((value & 0x04) << 2);
        updateChr(ctx);
        break;
      case 0xA000:
        ctx.window('prg-a000').setBank(value);
        break;
      case 0xC000:
        ctx.window('prg-c000').setBank(value);
        break;
      case 0xE000:
        state.chrBank0 = (state.chrBank0 & 0x10) | (value & 0x0F);
        updateChr(ctx);
        break;
      case 0xF000:
        state.chrBank1 = (state.chrBank1 & 0x10) | (value & 0x0F);
        updateChr(ctx);
        break;
    }
  }
};
