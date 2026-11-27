/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：Namco 108（mapper 154）的寄存器逻辑。
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

function update(ctx) {
  const { r } = readRegs(ctx);
  applyChr(ctx, [r[0] & 0x3E, (r[0] & 0x3F) | 1, r[1] & 0x3E, (r[1] & 0x3F) | 1, r[2] | 0x40, r[3] | 0x40, r[4] | 0x40, r[5] | 0x40]);
}

export default {
  reset(ctx) {
    update(ctx);
  },
  write(ctx, bus, addr, value) {
    ctx.nametable.set(value & 0x40 ? 'single1' : 'single0');
    if (addr >= 0xA000) return;
    const odd = addr & 0x01;
    mmc3(ctx).write(0x8000 | odd, odd ? value : value & 0x07);
    update(ctx);
  }
};
