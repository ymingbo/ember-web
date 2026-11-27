/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：MMC3_215的寄存器逻辑。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-17
 * 备注：寄存器写入先交给 MMC3 电路，再按附加寄存器重新计算各窗口；MMC3 电路管理 IRQ 计数器、镜像与 PRG RAM 保护。
 * ------------------------------------------------------------------------
 */

const PRG_IDS = ['prg-8000', 'prg-a000', 'prg-c000', 'prg-e000'];
const CHR_IDS = ['chr-0000', 'chr-0400', 'chr-0800', 'chr-0c00', 'chr-1000', 'chr-1400', 'chr-1800', 'chr-1c00'];

/** 设置窗口的页：16 位页号里 0xFFFE、0xFFFF 表示倒数第 2、第 1 页。 */
function setPage(ctx, id, page) {
  const window = ctx.window(id);
  let bank = page & 0xFFFF;
  if (bank >= 0x8000) bank -= 0x10000;
  if (bank < 0) bank += window.bankCount;
  window.setBank(bank);
}

function mmc3(ctx) {
  return ctx.circuit('mmc3');
}

/** 电路当前的 R0-R7 与选择寄存器。 */
function readRegs(ctx) {
  const circuit = mmc3(ctx);
  const r = [];
  for (let index = 0; index < 8; index += 1) r.push(circuit.get('r' + index));
  return { r, selector: circuit.get('selector') };
}

/** MMC3 的 4 个 8K 程序页（$8000 起）。 */
function basePrg(ctx) {
  const { r, selector } = readRegs(ctx);
  return selector & 0x40 ? [0xFFFE, r[7], r[6], 0xFFFF] : [r[6], r[7], 0xFFFE, 0xFFFF];
}

/** MMC3 的 8 个 1K 图案页（$0000 起）。 */
function baseChr(ctx) {
  const { r, selector } = readRegs(ctx);
  const pages = [r[0] & 0xFE, r[0] | 1, r[1] & 0xFE, r[1] | 1, r[2], r[3], r[4], r[5]];
  return selector & 0x80 ? pages.slice(4).concat(pages.slice(0, 4)) : pages;
}

function applyPrg(ctx, pages) {
  for (let index = 0; index < 4; index += 1) setPage(ctx, PRG_IDS[index], pages[index]);
}

function applyChr(ctx, pages) {
  for (let index = 0; index < 8; index += 1) setPage(ctx, CHR_IDS[index], pages[index]);
}

/** PRG RAM 允许且没有写保护。 */
function ramWritable(ctx) {
  const protect = mmc3(ctx).get('ramProtect');
  return (protect & 0x80) !== 0 && (protect & 0x40) === 0;
}

/** 直接写 PRG RAM（不经保护位）。 */
function writeRam(ctx, addr, value) {
  ctx.storage('prg-ram').write(addr & 0x1FFF, value);
}

const LUT_REG = [[0,1,2,3,4,5,6,7],[0,2,6,1,7,3,4,5],[0,5,4,1,7,2,6,3],[0,6,3,7,5,2,4,1],[0,2,5,3,6,1,7,4],[0,1,2,3,4,5,6,7],[0,1,2,3,4,5,6,7],[0,1,2,3,4,5,6,7]];
const LUT_ADDR = [[0,1,2,3,4,5,6,7],[3,2,0,4,1,5,6,7],[0,1,2,3,4,5,6,7],[5,0,1,2,3,7,6,4],[3,1,0,5,2,4,6,7],[0,1,2,3,4,5,6,7],[0,1,2,3,4,5,6,7],[0,1,2,3,4,5,6,7]];
const ex = [0, 3, 0];

function update(ctx) {
  const outerPrg = [];
  if (ex[0] & 0x80) {
    let mask = 0;
    let bank = ex[0] & 0x40 ? ((ex[1] & 0x03) << 4) | (ex[0] & 0x07) | ((ex[1] & 0x10) >> 1) : ((ex[1] & 0x03) << 4) | (ex[0] & 0x0F);
    bank = (bank << 1) & 0xFF;
    if (ex[0] & 0x20) applyPrg(ctx, [bank, bank + 1, bank + 2, bank + 3]);
    else applyPrg(ctx, [bank, bank + 1, bank, bank + 1]);
  } else {
    const mask = ex[0] & 0x40 ? 0x0F : 0x1F;
    const sbank = ex[0] & 0x40 ? ex[1] & 0x10 : 0;
    applyPrg(ctx, basePrg(ctx).map((page) => ((ex[1] & 0x03) << 5) | (page & mask) | sbank));
  }
  applyChr(ctx, baseChr(ctx).map((page) => (ex[0] & 0x40 ? ((ex[1] & 0x0C) << 6) | (page & 0x7F) | ((ex[1] & 0x20) << 2) : ((ex[1] & 0x0C) << 6) | page)));
}

export default {
  reset(ctx) {
    ex[0] = 0;
    ex[1] = 3;
    ex[2] = 0;
    update(ctx);
  },
  write(ctx, bus, addr, value) {
    if (addr < 0x8000) {
      if (addr === 0x5000) ex[0] = value;
      else if (addr === 0x5001) ex[1] = value;
      else if (addr === 0x5007) ex[2] = value;
    } else {
      const lut = LUT_ADDR[ex[2]][((addr >> 12) & 0x06) | (addr & 0x01)];
      const address = (lut & 0x01) | ((lut & 0x06) << 12) | 0x8000;
      let data = value;
      if (lut === 0) data = (value & 0xC0) | LUT_REG[ex[2]][value & 0x07];
      mmc3(ctx).write(address, data);
    }
    update(ctx);
  }
};
