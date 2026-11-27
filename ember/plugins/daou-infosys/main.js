/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：Daou Infosys的寄存器逻辑。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-17
 * 备注：寄存器写入后统一重新计算各窗口，与状态保持一致。
 * ------------------------------------------------------------------------
 */

const CHR = ["chr-0000","chr-0400","chr-0800","chr-0c00","chr-1000","chr-1400","chr-1800","chr-1c00"];

export default {
  write(ctx, bus, addr, value) {
    const state = ctx.state;
    if (addr <= 0xC00F) {
      const bank = (addr & 0x03) + (addr >= 0xC008 ? 4 : 0);
      state[((addr & 0x04) !== 0 ? 'chrHigh' : 'chrLow') + bank] = value;
      ctx.window(CHR[bank]).setBank((state['chrHigh' + bank] << 8) | state['chrLow' + bank]);
    } else if (addr === 0xC010) {
      ctx.window('prg-8000-16k').setBank(value);
    } else if (addr === 0xC014) {
      ctx.nametable.set((value & 0x01) !== 0 ? 'horizontal' : 'vertical');
    }
  }
};
