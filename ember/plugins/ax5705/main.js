/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：AX-5705的寄存器逻辑。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-17
 * 备注：寄存器写入后统一重新计算各窗口，与状态保持一致。
 * ------------------------------------------------------------------------
 */

const CHR = ["chr-0000","chr-0400","chr-0800","chr-0c00","chr-1000","chr-1400","chr-1800","chr-1c00"];
const CHR_INDEX = { 0xA008: 0, 0xA00A: 1, 0xC000: 2, 0xC002: 3, 0xC008: 4, 0xC00A: 5, 0xE000: 6, 0xE002: 7 };

// 写入数据线被打乱的 PRG bank 号。
function scrambled(value) {
  return ((value & 0x02) << 2) | ((value & 0x08) >> 2) | (value & 0x05);
}

function updateChrReg(ctx, index, value, low) {
  const name = 'chrReg' + index;
  const current = ctx.state[name];
  if (low) {
    ctx.state[name] = (current & 0xF0) | (value & 0x0F);
  } else {
    const high = ((value & 0x04) >> 1) | ((value & 0x02) << 1) | (value & 0x09);
    ctx.state[name] = (current & 0x0F) | ((high << 4) & 0xF0);
  }
  ctx.window(CHR[index]).setBank(ctx.state[name]);
}

export default {
  write(ctx, bus, addr, value) {
    if (addr >= 0xA008) {
      const key = addr & 0xF00E;
      if (key in CHR_INDEX) updateChrReg(ctx, CHR_INDEX[key], value, (addr & 0x01) === 0);
      return;
    }
    switch (addr & 0xF00F) {
      case 0x8000:
        ctx.window('prg-8000').setBank(scrambled(value));
        break;
      case 0x8008:
        ctx.nametable.set((value & 0x01) !== 0 ? 'horizontal' : 'vertical');
        break;
      case 0xA000:
        ctx.window('prg-a000').setBank(scrambled(value));
        break;
    }
  }
};
