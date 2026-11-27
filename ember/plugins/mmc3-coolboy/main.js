/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：MMC3_Coolboy的寄存器逻辑。
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

const ex = [0, 0, 0, 0];

function update(ctx) {
  const { r, selector } = readRegs(ctx);
  const prgMode = (selector >> 6) & 1;
  const chrMode = (selector >> 7) & 1;
  const mask0 = ((0x3F | (ex[1] & 0x40) | ((ex[1] & 0x20) << 2)) ^ ((ex[0] & 0x40) >> 2)) ^ ((ex[1] & 0x80) >> 2);
  const base = (ex[0] & 0x07) | ((ex[1] & 0x10) >> 1) | ((ex[1] & 0x0C) << 2) | ((ex[0] & 0x30) << 2);
  applyPrg(ctx, basePrg(ctx).map((page, slot) => {
    const addr = 0x8000 + slot * 0x2000;
    let p = page;
    if ((ex[3] & 0x40) && p >= 0xFE && prgMode) {
      if (slot === 1) p = 0;
      else if (slot === 3) p = 0;
    }
    let mask = mask0;
    if (!(ex[3] & 0x10)) return ((base << 4) & ~mask) | (p & mask);
    mask &= 0xF0;
    const emask = ex[1] & 0x02 ? (ex[3] & 0x0C) | ((addr & 0x4000) >> 13) : ex[3] & 0x0E;
    return ((base << 4) & ~mask) | (p & mask) | emask | (slot & 0x01);
  }));
  const cmask = 0xFF ^ (ex[0] & 0x80);
  const cbase = chrMode ? 0x1000 : 0;
  applyChr(ctx, baseChr(ctx).map((page, slot) => {
    const addr = slot * 0x400;
    let p = page;
    if (ex[3] & 0x10) {
      if (ex[3] & 0x40) {
        const key = cbase ^ addr;
        if (key === 0x0400 || key === 0x0C00) p &= 0x7F;
      }
      return (p & 0x80 & cmask) | (((ex[0] & 0x08) << 4) & ~cmask) | ((ex[2] & 0x0F) << 3) | slot;
    }
    if (ex[3] & 0x40) {
      const key = cbase ^ addr;
      if (key === 0x0000) p = r[0];
      else if (key === 0x0800) p = r[1];
      else if (key === 0x0400 || key === 0x0C00) p = 0;
    }
    return (p & cmask) | (((ex[0] & 0x08) << 4) & ~cmask);
  }));
}

export default {
  reset(ctx) {
    ex.fill(0);
    update(ctx);
  },
  write(ctx, bus, addr, value) {
    if (addr < 0x8000) {
      if (mmc3(ctx).get('ramProtect') & 0x80) writeRam(ctx, addr, value);
      if ((ex[3] & 0x90) !== 0x80) {
        ex[addr & 0x03] = value;
        update(ctx);
      }
      return;
    }
    mmc3(ctx).write(addr, value);
    update(ctx);
  }
};
