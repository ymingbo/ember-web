/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：MMC3_114的寄存器逻辑。
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

const SECURITY = [0, 3, 1, 5, 6, 7, 2, 4];
let ex0 = 0;
let ex1 = 0;

function update(ctx) {
  if (ex0 & 0x80) {
    const base = (ex0 & 0x0F) << 1;
    applyPrg(ctx, [base, base + 1, base, base + 1]);
  } else {
    applyPrg(ctx, basePrg(ctx));
  }
  applyChr(ctx, baseChr(ctx));
}

export default {
  reset(ctx) {
    ex0 = 0;
    ex1 = 0;
    update(ctx);
  },
  write(ctx, bus, addr, value) {
    const circuit = mmc3(ctx);
    if (addr < 0x8000) {
      ex0 = value;
    } else {
      switch (addr & 0xE001) {
        case 0x8001: circuit.write(0xA000, value); break;
        case 0xA000:
          circuit.write(0x8000, (value & 0xC0) | SECURITY[value & 0x07]);
          ex1 = 1;
          break;
        case 0xA001: circuit.write(0xC000, value); break;
        case 0xC000:
          if (ex1) {
            ex1 = 0;
            circuit.write(0x8001, value);
          }
          break;
        case 0xC001: circuit.write(0xC001, value); break;
        case 0xE000: circuit.write(0xE000, value); break;
        case 0xE001: circuit.write(0xE001, value); break;
      }
    }
    update(ctx);
  }
};
