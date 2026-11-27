/*
 * ------------------------------------------------------------------------
 * 名称：main.js
 * 说明：Bandai FCG的寄存器逻辑。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-17
 * 备注：IRQ 计数器按 CPU 周期递减，减到 0 的下一个周期触发：登记计时器，到点回调 onTimer；EEPROM 的 I2C 状态机在脚本里，数据放在带电池的存储区里；Datach 的条码读取器不做。
 * ------------------------------------------------------------------------
 */

const PRG_IDS = ['prg-8000-16k', 'prg-c000-16k'];
const CHR_IDS = ['chr-0000', 'chr-0400', 'chr-0800', 'chr-0c00', 'chr-1000', 'chr-1400', 'chr-1800', 'chr-1c00'];
const IDLE = 0;
const ADDRESS = 1;
const READ = 2;
const WRITE = 3;
const SEND_ACK = 4;
const WAIT_ACK = 5;
const CHIP_ADDRESS = 6;

function newChip(kind, storage) {
  return { kind, storage, mode: IDLE, next: IDLE, chip: 0, address: 0, data: 0, counter: 0, output: 0, prevScl: 0, prevSda: 0 };
}

/** 往 chip[key] 里移入一位：24C01 低位在前，24C02 高位在前。 */
function writeBit(c, key, value) {
  if (c.counter >= 8) return;
  const shift = c.kind === '24c01' ? c.counter : 7 - c.counter;
  c[key] = (c[key] & ~(1 << shift) & 0xFF) | (value << shift);
  c.counter += 1;
}

function readBit(c) {
  if (c.counter >= 8) return;
  const shift = c.kind === '24c01' ? c.counter : 7 - c.counter;
  c.output = (c.data & (1 << shift)) ? 1 : 0;
  c.counter += 1;
}

function write24c01(ctx, c, scl, sda) {
  const store = ctx.storage(c.storage);
  if (c.prevScl && scl && sda < c.prevSda) {
    c.mode = ADDRESS;
    c.address = 0;
    c.counter = 0;
    c.output = 1;
  } else if (c.prevScl && scl && sda > c.prevSda) {
    c.mode = IDLE;
    c.output = 1;
  } else if (scl > c.prevScl) {
    switch (c.mode) {
      case ADDRESS:
        if (c.counter < 7) {
          writeBit(c, 'address', sda);
        } else if (c.counter === 7) {
          c.counter = 8;
          if (sda) {
            c.next = READ;
            c.data = store.read(c.address & 0x7F);
          } else {
            c.next = WRITE;
          }
        }
        break;
      case SEND_ACK: c.output = 0; break;
      case READ: readBit(c); break;
      case WRITE: writeBit(c, 'data', sda); break;
      case WAIT_ACK:
        if (!sda) c.next = IDLE;
        break;
    }
  } else if (scl < c.prevScl) {
    switch (c.mode) {
      case ADDRESS:
        if (c.counter === 8) {
          c.mode = SEND_ACK;
          c.output = 1;
        }
        break;
      case SEND_ACK:
        c.mode = c.next;
        c.counter = 0;
        c.output = 1;
        break;
      case READ:
        if (c.counter === 8) {
          c.mode = WAIT_ACK;
          c.address = (c.address + 1) & 0x7F;
        }
        break;
      case WRITE:
        if (c.counter === 8) {
          c.mode = SEND_ACK;
          c.next = IDLE;
          store.write(c.address & 0x7F, c.data);
          c.address = (c.address + 1) & 0x7F;
        }
        break;
    }
  }
  c.prevScl = scl;
  c.prevSda = sda;
}

function write24c02(ctx, c, scl, sda) {
  const store = ctx.storage(c.storage);
  if (c.prevScl && scl && sda < c.prevSda) {
    c.mode = CHIP_ADDRESS;
    c.counter = 0;
    c.output = 1;
  } else if (c.prevScl && scl && sda > c.prevSda) {
    c.mode = IDLE;
    c.output = 1;
  } else if (scl > c.prevScl) {
    switch (c.mode) {
      case CHIP_ADDRESS: writeBit(c, 'chip', sda); break;
      case ADDRESS: writeBit(c, 'address', sda); break;
      case READ: readBit(c); break;
      case WRITE: writeBit(c, 'data', sda); break;
      case SEND_ACK: c.output = 0; break;
      case WAIT_ACK:
        if (!sda) {
          c.next = READ;
          c.data = store.read(c.address);
        }
        break;
    }
  } else if (scl < c.prevScl) {
    switch (c.mode) {
      case CHIP_ADDRESS:
        if (c.counter === 8) {
          if ((c.chip & 0xA0) === 0xA0) {
            c.mode = SEND_ACK;
            c.counter = 0;
            c.output = 1;
            if (c.chip & 0x01) {
              c.next = READ;
              c.data = store.read(c.address);
            } else {
              c.next = ADDRESS;
            }
          } else {
            c.mode = IDLE;
            c.counter = 0;
            c.output = 1;
          }
        }
        break;
      case ADDRESS:
        if (c.counter === 8) {
          c.counter = 0;
          c.mode = SEND_ACK;
          c.next = WRITE;
          c.output = 1;
        }
        break;
      case READ:
        if (c.counter === 8) {
          c.mode = WAIT_ACK;
          c.address = (c.address + 1) & 0xFF;
        }
        break;
      case WRITE:
        if (c.counter === 8) {
          c.counter = 0;
          c.mode = SEND_ACK;
          c.next = WRITE;
          store.write(c.address, c.data);
          c.address = (c.address + 1) & 0xFF;
        }
        break;
      case SEND_ACK:
      case WAIT_ACK:
        c.mode = c.next;
        c.counter = 0;
        c.output = 1;
        break;
    }
  }
  c.prevScl = scl;
  c.prevSda = sda;
}

function chipWrite(ctx, c, scl, sda) {
  if (c.kind === '24c01') write24c01(ctx, c, scl, sda);
  else write24c02(ctx, c, scl, sda);
}

let standard = null;
let extra = null;
const chrRegs = new Array(8).fill(0);
let prgPage = 0;
let prgBankSelect = 0;
let ramMapped = true;
const irq = { value: 0, reload: 0, enabled: false, start: 0 };

function liveCounter(ctx) {
  return irq.enabled ? (irq.value - (ctx.cycle() - irq.start)) & 0xFFFF : irq.value;
}

/** 计数器从 value 开始递减：减到 0 的下一个周期触发。 */
function setCounter(ctx, value, enabled) {
  irq.value = value & 0xFFFF;
  irq.enabled = enabled;
  irq.start = ctx.cycle();
  ctx.timer.cancel('irq');
  if (enabled) ctx.timer.after('irq', irq.value + 1);
}

function updatePrg(ctx) {
  ctx.window(PRG_IDS[0]).setBank(prgPage | prgBankSelect);
  ctx.window(PRG_IDS[1]).setBank(0x0F | prgBankSelect);
}

function isSubmapper4(ctx) {
  return ctx.header.mapper === 16 && ctx.header.variant === 4;
}

export default {
  reset(ctx, kind) {
    const header = ctx.header;
    if (kind === 'power') {
      chrRegs.fill(0);
      prgPage = 0;
      prgBankSelect = 0;
      ramMapped = true;
      standard = null;
      extra = null;
      if (header.mapper === 157) {
        standard = newChip('24c02', 'eeprom');
        if (!header.nes20 || header.prgNvramSize === 128) extra = newChip('24c01', 'eeprom1');
      } else if (header.mapper === 159) {
        standard = newChip('24c01', 'eeprom1');
      } else if (header.mapper === 16) {
        if (header.variant === 0 || (header.variant === 5 && header.prgNvramSize === 256)) standard = newChip('24c02', 'eeprom');
      }
      irq.reload = 0;
      setCounter(ctx, 0, false);
      ctx.window(PRG_IDS[0]).setBank(0);
      ctx.window(PRG_IDS[1]).setBank(0x0F);
      CHR_IDS.forEach((id) => ctx.window(id).setBank(0));
      ctx.window('ram-6000').setBank(0);
    } else {
      setCounter(ctx, liveCounter(ctx), irq.enabled);
    }
  },
  read(ctx, bus, addr) {
    if (ctx.header.mapper === 153) return ctx.peek(addr);
    let output = 0;
    if (extra && standard) output |= (standard.output && extra.output) ? 0x10 : 0;
    else if (standard) output |= standard.output << 4;
    return output;
  },
  write(ctx, bus, addr, value) {
    const header = ctx.header;
    const mapper = header.mapper;
    if (addr < 0x8000) {
      // $6000-$7FFF：mapper 16 的子 mapper 4 与默认型号是寄存器，其余没有寄存器（153 是 RAM）。
      if (mapper === 153) {
        if (ramMapped) ctx.storage('work').write(addr & 0x1FFF, value);
        return;
      }
      if (mapper !== 16 || header.variant === 5) return;
    } else if (mapper === 16 && header.variant === 4) {
      return;
    }
    switch (addr & 0x000F) {
      case 0x00: case 0x01: case 0x02: case 0x03: case 0x04: case 0x05: case 0x06: case 0x07: {
        const index = addr & 0x07;
        chrRegs[index] = value;
        if (mapper === 153 || ctx.window(PRG_IDS[0]).bankCount >= 0x20) {
          prgBankSelect = 0;
          for (let slot = 0; slot < 8; slot += 1) prgBankSelect |= (chrRegs[slot] & 0x01) << 4;
          updatePrg(ctx);
        } else if (header.chrSize !== 0 && mapper !== 157) {
          ctx.window(CHR_IDS[index]).setBank(value);
        }
        if (extra && mapper === 157 && index <= 3) chipWrite(ctx, extra, (value >> 3) & 0x01, extra.prevSda);
        break;
      }
      case 0x08:
        prgPage = value & 0x0F;
        updatePrg(ctx);
        break;
      case 0x09:
        ctx.nametable.set(['vertical', 'horizontal', 'single0', 'single1'][value & 0x03]);
        break;
      case 0x0A:
        setCounter(ctx, isSubmapper4(ctx) ? liveCounter(ctx) : irq.reload, (value & 0x01) !== 0);
        ctx.irq.release();
        break;
      case 0x0B:
        if (isSubmapper4(ctx)) setCounter(ctx, (liveCounter(ctx) & 0xFF00) | value, irq.enabled);
        else irq.reload = (irq.reload & 0xFF00) | value;
        break;
      case 0x0C:
        if (isSubmapper4(ctx)) setCounter(ctx, (liveCounter(ctx) & 0x00FF) | (value << 8), irq.enabled);
        else irq.reload = (irq.reload & 0x00FF) | (value << 8);
        break;
      case 0x0D:
        if (mapper === 153) {
          const ram = ctx.window('ram-6000');
          ramMapped = (value & 0x20) !== 0;
          if (ramMapped) ram.setBank(0);
          else ram.unmap();
        } else {
          const scl = (value & 0x20) >> 5;
          const sda = (value & 0x40) >> 6;
          if (standard) chipWrite(ctx, standard, scl, sda);
          if (extra) chipWrite(ctx, extra, extra.prevScl, sda);
        }
        break;
    }
  },
  onTimer(ctx) {
    ctx.irq.assert();
    // 触发的那个周期里计数器从 0 减到 $FFFF，之后每 65536 个周期再触发。
    setCounter(ctx, 0xFFFF, true);
  }
};
