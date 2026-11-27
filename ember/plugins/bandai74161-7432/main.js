/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：Bandai 74161/7432的寄存器逻辑。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-17
 * 备注：寄存器写入后统一重新计算各窗口，与状态保持一致。
 * ------------------------------------------------------------------------
 */

export default {
  reset(ctx, kind) {
    if (kind !== 'power') return;
    ctx.state.mirroringControl = ctx.header.mapper === 152 ? 1 : 0;
    ctx.nametable.set('vertical');
  },
  write(ctx, bus, addr, value) {
    const mirroringBit = (value & 0x80) !== 0;
    // 只要有游戏写过为 1 的位 7，就认为它会用名称表切换。
    if (mirroringBit) ctx.state.mirroringControl = 1;
    if (ctx.state.mirroringControl !== 0) ctx.nametable.set(mirroringBit ? 'single1' : 'single0');
    ctx.window('prg-8000-16k').setBank((value >> 4) & 0x07);
    ctx.window('chr-0000').setBank(value & 0x0F);
  }
};
