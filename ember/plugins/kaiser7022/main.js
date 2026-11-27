/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：Kaiser KS-7022的寄存器逻辑。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-17
 * 备注：寄存器写入后统一重新计算各窗口，与状态保持一致。
 * ------------------------------------------------------------------------
 */

// 把暂存的 bank 号放到两个 16K 的 PRG 窗口与 CHR 窗口。
function apply(ctx) {
  ctx.window('chr-0000').setBank(ctx.state.reg);
  ctx.window('prg-8000-16k').setBank(ctx.state.reg);
  ctx.window('prg-c000-16k').setBank(ctx.state.reg);
}

export default {
  reset(ctx) {
    ctx.state.reg = 0;
    apply(ctx);
  },
  read(ctx, bus, addr) {
    apply(ctx);
    return ctx.peek(addr);
  },
  write(ctx, bus, addr, value) {
    if (addr === 0x8000) ctx.nametable.set((value & 0x04) !== 0 ? 'horizontal' : 'vertical');
    else if (addr === 0xA000) ctx.state.reg = value & 0x0F;
  }
};
