/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：Mapper 116的寄存器逻辑。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-17
 * 备注：三种模式的寄存器各自保存在脚本里；MMC3 模式的寄存器、IRQ 与镜像位由 MMC3 电路实现。
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

let mode = 0;
const vrcChr = [0xFF, 0xFF, 0xFF, 0xFF, 4, 5, 6, 7];
const vrcPrg = [0, 1];
let vrcMirroring = 0;
const mmc1 = [0x0C, 0, 0, 0];
let buffer = 0;
let shift = 0;

function update(ctx) {
  const outer = (mode & 0x04) << 6;
  switch (mode & 0x03) {
    case 0:
      applyPrg(ctx, [vrcPrg[0], vrcPrg[1], 0xFFFE, 0xFFFF]);
      applyChr(ctx, vrcChr.map((page) => outer | page));
      ctx.nametable.set(vrcMirroring & 0x01 ? 'horizontal' : 'vertical');
      break;
    case 1:
      applyPrg(ctx, basePrg(ctx));
      applyChr(ctx, baseChr(ctx).map((page) => outer | page));
      ctx.nametable.set(mmc3(ctx).get('horizontalMirroring') ? 'horizontal' : 'vertical');
      break;
    default: {
      const bank = mmc1[3] & 0x0F;
      if (mmc1[0] & 0x08) {
        if (mmc1[0] & 0x04) applyPrg(ctx, [bank << 1, (bank << 1) + 1, 0x1E, 0x1F]);
        else applyPrg(ctx, [0, 1, bank << 1, (bank << 1) + 1]);
      } else {
        const base = (bank & 0xFE) << 1;
        applyPrg(ctx, [base, base + 1, base + 2, base + 3]);
      }
      if (mmc1[0] & 0x10) {
        const low = mmc1[1] << 2;
        const high = mmc1[2] << 2;
        applyChr(ctx, [low, low + 1, low + 2, low + 3, high, high + 1, high + 2, high + 3]);
      } else {
        const base = (mmc1[1] & 0xFE) << 2;
        applyChr(ctx, [0, 1, 2, 3, 4, 5, 6, 7].map((index) => base + index));
      }
      ctx.nametable.set(['single0', 'single1', 'vertical', 'horizontal'][mmc1[0] & 0x03]);
      break;
    }
  }
}

function writeVrc(addr, value) {
  if (addr >= 0xB000 && addr <= 0xE003) {
    const index = ((((addr & 0x02) | (addr >> 10)) >> 1) + 2) & 0x07;
    const shiftBits = (addr & 1) << 2;
    vrcChr[index] = (vrcChr[index] & (0xF0 >> shiftBits)) | ((value & 0x0F) << shiftBits);
  } else {
    switch (addr & 0xF000) {
      case 0x8000: vrcPrg[0] = value; break;
      case 0xA000: vrcPrg[1] = value; break;
      case 0x9000: vrcMirroring = value; break;
    }
  }
}

function writeMmc1(addr, value) {
  if (value & 0x80) {
    mmc1[0] |= 0x0C;
    buffer = 0;
    shift = 0;
  } else {
    buffer |= (value & 0x01) << shift;
    shift += 1;
    if (shift === 5) {
      mmc1[(addr >> 13) - 4] = buffer;
      buffer = 0;
      shift = 0;
    }
  }
}

export default {
  reset(ctx) {
    mode = 0;
    vrcChr.splice(0, 8, 0xFF, 0xFF, 0xFF, 0xFF, 4, 5, 6, 7);
    vrcPrg[0] = 0;
    vrcPrg[1] = 1;
    vrcMirroring = 0;
    mmc1[0] = 0x0C;
    mmc1[1] = 0;
    mmc1[2] = 0;
    mmc1[3] = 0;
    buffer = 0;
    shift = 0;
    update(ctx);
  },
  write(ctx, bus, addr, value) {
    if (addr < 0x8000) {
      if ((addr & 0x4100) === 0x4100) {
        mode = value;
        if (addr & 0x01) {
          mmc1[0] = 0x0C;
          mmc1[3] = 0;
          buffer = 0;
          shift = 0;
        }
      }
    } else if ((mode & 0x03) === 0) {
      writeVrc(addr, value);
    } else if ((mode & 0x03) === 1) {
      mmc3(ctx).write(addr, value);
    } else {
      writeMmc1(addr, value);
    }
    update(ctx);
  }
};
