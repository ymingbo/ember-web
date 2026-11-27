/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：Mapper 108的寄存器逻辑。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-17
 * 备注：寄存器写入后统一重新计算各窗口，与状态保持一致。
 * ------------------------------------------------------------------------
 */

export default {
  reset(ctx, kind) {
    if (kind === 'power') ctx.state.prgReg = 0xFF;
  },
  write(ctx, bus, addr, value) {
    if ((addr & 0x9000) === 0x8000 || addr >= 0xF000) {
      // 有的游戏要求 $F000 以上的写入也切换 PRG。
      ctx.state.prgReg = value;
      ctx.state.chrReg = value;
    } else {
      ctx.state.chrReg = value & 0x01;
    }
    ctx.window('prg-6000').setBank(ctx.state.prgReg);
    ctx.window('chr-0000').setBank(ctx.state.chrReg);
  }
};
