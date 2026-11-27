/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：Mapper 103的寄存器逻辑。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-17
 * 备注：寄存器写入后统一重新计算各窗口，与状态保持一致。
 * ------------------------------------------------------------------------
 */

const BANK = 8192;

// 固定映射最后 4 个 8K bank：用字节偏移摆放，不受窗口不对齐的影响。
function placeFixedRom(ctx) {
  const size = ctx.storage('prg').size;
  ctx.window('prg-8000').setOffset(size - 4 * BANK);
  ctx.window('prg-a000').setOffset(size - 3 * BANK);
  ctx.window('prg-d800').setOffset(size - 2 * BANK + 0x1800);
  ctx.window('prg-e000').setOffset(size - BANK);
}

function update(ctx) {
  placeFixedRom(ctx);
  if (ctx.state.prgRamDisabled !== 0) {
    ctx.window('ram-6000').select('prg', ctx.state.prgReg);
    ctx.window('ram-b800').select('prg');
    ctx.window('ram-b800').setOffset(ctx.storage('prg').size - 3 * BANK + 0x1800);
  } else {
    ctx.window('ram-6000').select('wram', 0);
    ctx.window('ram-b800').select('wram', 1);
  }
}

// 工作 RAM 在这些地址上始终可写，即使当前映射的是 PRG ROM。
function writeWorkRam(ctx, offset, value) {
  const ram = ctx.storage('wram');
  if (offset < ram.size) ram.write(offset, value);
}

export default {
  reset(ctx, kind) {
    if (kind === 'power') update(ctx);
  },
  write(ctx, bus, addr, value) {
    switch (addr & 0xF000) {
      case 0x6000:
      case 0x7000:
        writeWorkRam(ctx, addr - 0x6000, value);
        break;
      case 0x8000:
        ctx.state.prgReg = value & 0x0F;
        update(ctx);
        break;
      case 0xB000:
      case 0xC000:
      case 0xD000:
        if (addr >= 0xB800 && addr < 0xD800) writeWorkRam(ctx, 0x2000 + addr - 0xB800, value);
        break;
      case 0xE000:
        ctx.nametable.set((value & 0x08) !== 0 ? 'horizontal' : 'vertical');
        break;
      case 0xF000:
        ctx.state.prgRamDisabled = (value & 0x10) !== 0 ? 1 : 0;
        update(ctx);
        break;
    }
  }
};
