/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：MMC3_219的寄存器逻辑。
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

let ex0 = 0;
let ex1 = 0;
let ex2 = 0;

function init(ctx) {
  applyPrg(ctx, [0xFFFC, 0xFFFD, 0xFFFE, 0xFFFF]);
  applyChr(ctx, [0, 1, 2, 3, 4, 5, 6, 7]);
}

export default {
  reset(ctx) {
    ex0 = 0;
    ex1 = 0;
    ex2 = 0;
    init(ctx);
  },
  write(ctx, bus, addr, value) {
    if (addr >= 0xA000) {
      mmc3(ctx).write(addr, value);
      return;
    }
    switch (addr & 0xE003) {
      case 0x8000:
        ex0 = 0;
        ex1 = value;
        break;
      case 0x8001: {
        if (ex0 >= 0x23 && ex0 <= 0x26) {
          const bank = ((value & 0x20) >> 5) | ((value & 0x10) >> 3) | ((value & 0x08) >> 1) | ((value & 0x04) << 1);
          setPage(ctx, PRG_IDS[0x26 - ex0], bank);
        }
        const high = (value >> 1) & 0x0F;
        switch (ex1) {
          case 0x08: case 0x0A: case 0x0E: case 0x12: case 0x16: case 0x1A: case 0x1E:
            ex2 = (value << 4) & 0xFF;
            break;
          case 0x09: setPage(ctx, CHR_IDS[0], ex2 | (value >> 1 & 0x0E)); break;
          case 0x0B: setPage(ctx, CHR_IDS[1], ex2 | (value >> 1 | 0x01)); break;
          case 0x0C: case 0x0D: setPage(ctx, CHR_IDS[2], ex2 | (value >> 1 & 0x0E)); break;
          case 0x0F: setPage(ctx, CHR_IDS[3], ex2 | (value >> 1 | 0x01)); break;
          case 0x10: case 0x11: setPage(ctx, CHR_IDS[4], ex2 | (value >> 1 & 0x0F)); break;
          case 0x14: case 0x15: setPage(ctx, CHR_IDS[5], ex2 | (value >> 1 & 0x0F)); break;
          case 0x18: case 0x19: setPage(ctx, CHR_IDS[6], ex2 | (value >> 1 & 0x0F)); break;
          case 0x1C: case 0x1D: setPage(ctx, CHR_IDS[7], ex2 | (value >> 1 & 0x0F)); break;
        }
        break;
      }
      case 0x8002:
        ex0 = value;
        ex1 = 0;
        break;
    }
  }
};
