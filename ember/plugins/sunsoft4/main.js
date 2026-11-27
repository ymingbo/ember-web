/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：Sunsoft 4的寄存器逻辑。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-17
 * 备注：外部 ROM 只在最后一次写 $6000-$7FFF 之后的 105 * 1024 个 CPU 周期里可见：每次写时重新起一个计时，到点回调 onTimer 取消映射。
 * ------------------------------------------------------------------------
 */

const CHR_IDS = ['chr-0000', 'chr-0800', 'chr-1000', 'chr-1800'];
const ntRegs = [0, 0];
let mirroring = 'vertical';
let useChrForNametables = false;
let ramEnabled = false;
let usingExternal = false;
let externalPage = 0;
let licensed = false;

function updateNametables(ctx) {
  if (useChrForNametables) {
    for (let slot = 0; slot < 4; slot += 1) {
      let reg = 0;
      if (mirroring === 'vertical') reg = slot & 0x01;
      else if (mirroring === 'horizontal') reg = (slot & 0x02) >> 1;
      else if (mirroring === 'single1') reg = 1;
      ctx.nametable.map(slot, 'chr', ntRegs[reg]);
    }
  } else {
    ctx.nametable.set(mirroring);
  }
}

function updateState(ctx) {
  const ram = ctx.window('prg-ram');
  if (ramEnabled) ram.setBank(0);
  else ram.unmap();
  if (usingExternal) {
    const rom = ctx.window('prg-8000-16k');
    if (licensed) rom.setBank(externalPage);
    else rom.unmap();
  }
}

export default {
  reset(ctx) {
    ntRegs[0] = 0;
    ntRegs[1] = 0;
    mirroring = ctx.header.mirroring === 'horizontal' ? 'horizontal' : 'vertical';
    useChrForNametables = false;
    ramEnabled = false;
    usingExternal = false;
    licensed = false;
    ctx.timer.cancel('license');
    updateNametables(ctx);
    updateState(ctx);
  },
  write(ctx, bus, addr, value) {
    if (addr < 0x8000) {
      if (ramEnabled) ctx.storage('prg-ram').write(addr & 0x1FFF, value);
      licensed = true;
      ctx.timer.after('license', 1024 * 105);
      updateState(ctx);
      return;
    }
    switch (addr & 0xF000) {
      case 0x8000: ctx.window(CHR_IDS[0]).setBank(value); break;
      case 0x9000: ctx.window(CHR_IDS[1]).setBank(value); break;
      case 0xA000: ctx.window(CHR_IDS[2]).setBank(value); break;
      case 0xB000: ctx.window(CHR_IDS[3]).setBank(value); break;
      case 0xC000:
        ntRegs[0] = value | 0x80;
        updateNametables(ctx);
        break;
      case 0xD000:
        ntRegs[1] = value | 0x80;
        updateNametables(ctx);
        break;
      case 0xE000:
        mirroring = ['vertical', 'horizontal', 'single0', 'single1'][value & 0x03];
        useChrForNametables = (value & 0x10) !== 0;
        updateNametables(ctx);
        break;
      case 0xF000: {
        const window = ctx.window('prg-8000-16k');
        const pages = window.bankCount;
        if ((value & 0x08) === 0 && pages > 8) {
          usingExternal = true;
          externalPage = 0x08 | ((value & 0x07) % (pages - 0x08));
          window.setBank(externalPage);
        } else {
          usingExternal = false;
          window.setBank(value & 0x07);
        }
        ramEnabled = (value & 0x10) !== 0;
        updateState(ctx);
        break;
      }
    }
  },
  onTimer(ctx) {
    licensed = false;
    updateState(ctx);
  }
};
