/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：MMC1的寄存器逻辑。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-17
 * 备注：串行寄存器与各寄存器保存在状态里，每次写入后统一重新计算各窗口；上电时控制寄存器的位 2、3 为 1，使 $C000 固定为最后一个 bank。
 * ------------------------------------------------------------------------
 */

const MIRRORING = ['single0', 'single1', 'vertical', 'horizontal'];
let lastWriteCycle = -1000;

/** 按当前寄存器重新计算全部窗口。 */
function update(ctx) {
  const state = ctx.state;
  const header = ctx.header;
  const chrMode = (state.ctrl >> 4) & 1;
  const prgMode = (state.ctrl >> 3) & 1;
  const slotSelect = (state.ctrl >> 2) & 1;
  const extra = state.lastChr1 && chrMode ? state.chr1 : state.chr0;

  ctx.nametable.set(MIRRORING[state.ctrl & 3]);

  // 512K 的卡（SUROM）用 CHR 寄存器的位 4 选 256K 的一半。
  const select = header.prgSize === 0x80000 ? extra & 0x10 : 0;
  const prg0 = ctx.window('prg-8000-16k');
  const prg1 = ctx.window('prg-c000-16k');
  if (header.variant === 5) {
    // 子 mapper 5：固定 32K，没有 PRG 切换。
    prg0.setBank(0);
    prg1.setBank(1);
  } else if (prgMode) {
    if (slotSelect) {
      prg0.setBank((state.prg & 0x0F) | select);
      prg1.setBank(0x0F | select);
    } else {
      prg0.setBank(select);
      prg1.setBank((state.prg & 0x0F) | select);
    }
  } else {
    prg0.setBank((state.prg & 0x0E) | select);
    prg1.setBank((state.prg & 0x0E) | select | 1);
  }

  if (chrMode) {
    ctx.window('chr-0000').setBank(state.chr0);
    ctx.window('chr-1000').setBank(state.chr1);
  } else {
    ctx.window('chr-0000').setBank(state.chr0 & 0x1E);
    ctx.window('chr-1000').setBank((state.chr0 & 0x1E) + 1);
  }

  // $6000-$7FFF 的 RAM：32K（SXROM）按 CHR 位 2-3 选 8K，8K+8K（SOROM）按 CHR 位 3 选存档或工作，其余 8K。
  const save = ctx.storage('save').size;
  const work = ctx.storage('work').size;
  const total = save + work;
  const ram = ctx.window('ram-6000');
  const disabled = header.mapper !== 155 && (state.prg & 0x10) !== 0;
  const main = save > 0 ? 'save' : 'work';
  if (total === 0 || disabled) ram.unmap();
  else if (total > 0x4000) ram.select(main, (extra >> 2) & 3);
  else if (total > 0x2000) {
    if (save === 0x2000 && work === 0x2000) ram.select((extra >> 3) & 1 ? 'work' : 'save', 0);
    else ram.select(main, (extra >> 2) & 1);
  } else ram.select(main, 0);
}

/** 串行寄存器写满 5 位后写入对应的寄存器。 */
function commit(ctx, addr, value) {
  const state = ctx.state;
  switch (addr & 0xE000) {
    case 0x8000: state.ctrl = value; break;
    case 0xA000: state.lastChr1 = 0; state.chr0 = value; break;
    case 0xC000: state.lastChr1 = 1; state.chr1 = value; break;
    case 0xE000: state.prg = value; break;
  }
}

export default {
  reset(ctx, kind) {
    if (kind !== 'power') return;
    lastWriteCycle = -1000;
    const state = ctx.state;
    state.ctrl = 0x0C;
    update(ctx);
  },
  write(ctx, bus, addr, value) {
    const now = ctx.cycle();
    // 与上一次写入相距不到 2 个周期的写入（如读改写指令的第二次写）被忽略；复位位照常处理。
    const accepted = (value & 0x80) !== 0 || now - lastWriteCycle >= 2;
    lastWriteCycle = now;
    if (!accepted) return;
    const state = ctx.state;
    if (value & 0x80) {
      state.shiftReg = 0;
      state.shiftCount = 0;
      state.ctrl = state.ctrl | 0x0C;
    } else {
      state.shiftReg = (state.shiftReg >> 1) | ((value << 4) & 0x10);
      state.shiftCount = state.shiftCount + 1;
      if (state.shiftCount === 5) {
        commit(ctx, addr, state.shiftReg);
        state.shiftReg = 0;
        state.shiftCount = 0;
      }
    }
    update(ctx);
  }
};
