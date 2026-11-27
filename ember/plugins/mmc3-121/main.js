/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：MMC3_121的寄存器逻辑。
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

const LOOKUP = [0x83, 0x83, 0x42, 0x00];
const ex = [0, 0, 0, 0x80, 0, 0, 0, 0];

function updateEx() {
  switch (ex[5] & 0x3F) {
    case 0x20: case 0x29: case 0x2B: case 0x3C: case 0x3F:
      ex[7] = 1;
      ex[0] = ex[6];
      break;
    case 0x26:
      ex[7] = 0;
      ex[0] = ex[6];
      break;
    case 0x2C:
      ex[7] = 1;
      if (ex[6]) ex[0] = ex[6];
      break;
    case 0x28:
      ex[7] = 0;
      ex[1] = ex[6];
      break;
    case 0x2A:
      ex[7] = 0;
      ex[2] = ex[6];
      break;
    case 0x2F:
      break;
    default:
      ex[5] = 0;
      break;
  }
}

function update(ctx) {
  const or = (ex[3] & 0x80) >> 2;
  const base = basePrg(ctx);
  if (ex[5] & 0x3F) {
    applyPrg(ctx, [(base[0] & 0x1F) | or, ex[2] | or, ex[1] | or, ex[0] | or]);
  } else {
    applyPrg(ctx, base.map((page) => (page & 0x1F) | or));
  }
  const same = ctx.header.prgSize === ctx.header.chrSize;
  applyChr(ctx, baseChr(ctx).map((page, slot) => (same ? page | ((ex[3] & 0x80) << 1) : slot >= 4 ? page | 0x100 : page)));
}

export default {
  reset(ctx) {
    ex.fill(0);
    ex[3] = 0x80;
    update(ctx);
  },
  read() {
    return ex[4];
  },
  write(ctx, bus, addr, value) {
    const circuit = mmc3(ctx);
    if (addr < 0x8000) {
      ex[4] = LOOKUP[value & 0x03];
      if ((addr & 0x5180) === 0x5180) ex[3] = value;
    } else if (addr < 0xA000) {
      if ((addr & 0x03) === 0x03) {
        ex[5] = value;
        updateEx();
        circuit.write(0x8000, value);
      } else if (addr & 0x01) {
        ex[6] = ((value & 0x01) << 5) | ((value & 0x02) << 3) | ((value & 0x04) << 1) | ((value & 0x08) >> 1) | ((value & 0x10) >> 3) | ((value & 0x20) >> 5);
        if (!ex[7]) updateEx();
        circuit.write(0x8001, value);
      } else {
        circuit.write(0x8000, value);
      }
    } else {
      circuit.write(addr, value);
    }
    update(ctx);
  }
};
