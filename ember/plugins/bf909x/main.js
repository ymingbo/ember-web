/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：Codemasters BF909x的寄存器逻辑。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-17
 * 备注：寄存器写入后统一重新计算各窗口，与状态保持一致。
 * ------------------------------------------------------------------------
 */

export default {
  reset(ctx, kind) {
    if (kind === 'power') ctx.state.bf9097Mode = ctx.header.variant === 1 ? 1 : 0;
  },
  write(ctx, bus, addr, value) {
    // 火鹰（Fire Hawk）用 $9000 切换名称表，据此识别 BF9097。
    if (addr === 0x9000) ctx.state.bf9097Mode = 1;
    if (addr >= 0xC000 || ctx.state.bf9097Mode === 0) ctx.window('prg-8000-16k').setBank(value);
    else ctx.nametable.set((value & 0x10) !== 0 ? 'single0' : 'single1');
  }
};
