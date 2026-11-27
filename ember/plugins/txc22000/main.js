/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：TXC 22000的寄存器逻辑。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-17
 * 备注：TXC 芯片的累加器、暂存、反相器与输出锁存逻辑写在脚本里；读寄存器时未被芯片驱动的数据线（开放总线的位）按 0 返回。
 * ------------------------------------------------------------------------
 */

const txc = { acc: 0, inv: 0, staging: 0, out: 0, increase: false, y: false, invert: false, mask: 0x07 };

function txcRead() {
  const value = ((txc.acc & txc.mask) | ((txc.inv ^ (txc.invert ? 0xFF : 0)) & ~txc.mask)) & 0xFF;
  txc.y = !txc.invert || (value & 0x10) !== 0;
  return value;
}

function txcWrite(addr, value) {
  if (addr < 0x8000) {
    switch (addr & 0xE103) {
      case 0x4100:
        if (txc.increase) txc.acc = (txc.acc + 1) & 0xFF;
        else txc.acc = (((txc.acc & ~txc.mask) | (txc.staging & txc.mask)) ^ (txc.invert ? 0xFF : 0)) & 0xFF;
        break;
      case 0x4101:
        txc.invert = (value & 0x01) !== 0;
        break;
      case 0x4102:
        txc.staging = value & txc.mask;
        txc.inv = value & ~txc.mask & 0xFF;
        break;
      case 0x4103:
        txc.increase = (value & 0x01) !== 0;
        break;
    }
  } else {
    txc.out = (txc.acc & 0x0F) | ((txc.inv & 0x08) << 1);
  }
  txc.y = !txc.invert || (value & 0x10) !== 0;
}

function txcReset() {
  txc.acc = 0;
  txc.inv = 0;
  txc.staging = 0;
  txc.out = 0;
  txc.increase = false;
  txc.y = false;
  txc.invert = false;
}

const convert = (v) => ((v & 0x01) << 5) | ((v & 0x02) << 3) | ((v & 0x04) << 1) | ((v & 0x08) >> 1) | ((v & 0x10) >> 3) | ((v & 0x20) >> 5);

let chrBank = 0;

function update(ctx) {
  ctx.window('prg-8000-32k').setBank(txc.out & 0x03);
  ctx.window('chr-0000').setBank(chrBank);
}

export default {
  reset(ctx) {
    txcReset();
    chrBank = 0;
    update(ctx);
  },
  read(ctx, bus, addr) {
    const value = (addr & 0x103) === 0x100 ? (txcRead() << 4) & 0x30 : 0;
    update(ctx);
    return value;
  },
  write(ctx, bus, addr, value) {
    if ((addr & 0xF200) === 0x4200) chrBank = value;
    txcWrite(addr, (value >> 4) & 0x03);
    update(ctx);
  }
};
