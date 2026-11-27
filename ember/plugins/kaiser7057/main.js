/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：Kaiser KS-7057的寄存器逻辑。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-17
 * 备注：寄存器写入后统一重新计算各窗口，与状态保持一致。
 * ------------------------------------------------------------------------
 */

const ROM6000 = ["prg-6000","prg-6800","prg-7000","prg-7800"];
const SLOTS = ["prg-8000","prg-8800","prg-9000","prg-9800"];
const REGISTER_INDEX = { 0xB000: 0, 0xB002: 1, 0xC000: 2, 0xC002: 3, 0xD000: 4, 0xD002: 5, 0xE000: 6, 0xE002: 7 };

function update(ctx) {
  for (let index = 0; index < 4; index += 1) {
    ctx.window(ROM6000[index]).setBank(ctx.state['reg' + (4 + index)]);
    ctx.window(SLOTS[index]).setBank(ctx.state['reg' + index]);
  }
}

export default {
  write(ctx, bus, addr, value) {
    const key = addr & 0xF002;
    if (key === 0x8000 || key === 0x8002 || key === 0x9000 || key === 0x9002) {
      ctx.nametable.set((value & 0x01) !== 0 ? 'vertical' : 'horizontal');
      return;
    }
    if (!(key in REGISTER_INDEX)) return;
    const name = 'reg' + REGISTER_INDEX[key];
    const current = ctx.state[name];
    ctx.state[name] = (addr & 0x01) === 0 ? (current & 0xF0) | (value & 0x0F) : (current & 0x0F) | ((value << 4) & 0xF0);
    update(ctx);
  }
};
