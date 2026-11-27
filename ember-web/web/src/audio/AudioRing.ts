/*
 * ------------------------------------------------------------------------
 * 名称：audioRing.ts
 * 说明：音频环形缓冲，照 fceux Simulate/waveout.c 的 write_waveOutBuffer 移植。
 *
 *   为什么需要它：核心的 ember_get_audio_samples 每次都把积累的采样**全取走**
 *   （apu-2a03.cpp:203 的 samples_.swap），前端 ScriptProcessor 每次硬要 2048 个，
 *   不够就用 0 补齐（useEmulator.ts 里的 output.fill(0, samplesRead)）——
 *   少一个采样就补一个静音，听感就是断续。
 *
 *   fceux 不断续的原因就是它有一个 32 块的环形缓冲池（BUFFER_SIZE=1800 字节 ×
 *   BUFFER_COUNT=32），模拟器算快算慢都由这个池子吸收：
 *     · get_space() 返回剩余空间，写不下就返回 0，**绝不补零**
 *     · 声卡播完一块自动归还（WOM_DONE 回调里 --full_buffers）
 *
 *   这里照抄它的参数：
 *     BUFFER_SIZE  1800 字节 → 单声道 float 为 450 个采样
 *     BUFFER_COUNT 32 块
 *     48kHz 下总深度 = 32 × 450 / 48000 ≈ 300 毫秒
 *
 *   与 fceux 的差异（受 Web Audio 限制，必须说明）：
 *     fceux 的消费端是声卡回调，播完自动还块，生产者永远有空间可写。
 *     ScriptProcessor 的 onaudioprocess 是"拉"模式——回调必须**填满**整个
 *     outputBuffer 否则浏览器播静音。所以这里多一层保护：
 *     环里数据够就取，不够就重复上一次的最后一个采样（保持电平连续），
 *     而不是丢零。数据积压过多时丢最旧的（fceux 里写不下就直接返回，不会积压）。
 * ------------------------------------------------------------------------
 */

/** 单块字节数，照 fceux waveout.c:32 的 BUFFER_SIZE。 */
const BUFFER_SIZE = 1800;
/** 块数，照 fceux waveout.c:33 的 BUFFER_COUNT。 */
const BUFFER_COUNT = 32;
/** 单声道 float 每个采样 4 字节，所以每块 450 个采样。 */
const SAMPLES_PER_BLOCK = BUFFER_SIZE / 4;
/** 缓冲总容量（采样数）。 */
const CAPACITY = SAMPLES_PER_BLOCK * BUFFER_COUNT;

export class AudioRing {
  private readonly buf: Float32Array;
  /** 写指针（下一个要写的位置）。 */
  private writePos = 0;
  /** 读指针（下一个要读的位置）。 */
  private readPos = 0;
  /** 当前积压的采样数。 */
  private count = 0;
  /** 上一次输出的最后一个采样，用于供给不足时保持电平连续。 */
  private lastSample = 0;

  constructor() {
    this.buf = new Float32Array(CAPACITY);
  }

  /** 剩余可写空间（采样数），对应 fceux 的 get_space()。 */
  get space(): number {
    return CAPACITY - this.count;
  }

  /** 当前积压（采样数），对应 fceux 的 get_holds()。 */
  get holds(): number {
    return this.count;
  }

  /** 缓冲深度（毫秒），供自检用。 */
  depthMs(sampleRate: number): number {
    return (this.count / sampleRate) * 1000;
  }

  /**
   * 写入采样，对应 fceux 的 write_waveOutBuffer。
   * **写不下就只写能写的部分，绝不补零**——这是 fceux 不断续的核心。
   * @returns 实际写入的采样数
   */
  write(src: Float32Array, len: number): number {
    if (len <= 0) return 0;
    /* fceux:227 的 if (buffered_bytes == BUFFER_COUNT*BUFFER_SIZE) return 0; */
    let remaining = Math.min(len, this.space);
    const written = remaining;
    let srcOff = 0;
    while (remaining > 0) {
      /* 单次最多写到缓冲末尾，然后回绕 */
      const chunk = Math.min(remaining, CAPACITY - this.writePos);
      this.buf.set(src.subarray(srcOff, srcOff + chunk), this.writePos);
      this.writePos = (this.writePos + chunk) % CAPACITY;
      srcOff += chunk;
      remaining -= chunk;
    }
    this.count += written;
    return written;
  }

  /**
   * 读出采样填满 dst，对应 ScriptProcessor 的 onaudioprocess。
   *
   * fceux 里不存在"取不满"的情况（缓冲够深 + 声卡回调自动还块）；
   * 这里必须填满整个 outputBuffer，所以：
   *   · 环里有数据 → 直接取
   *   · 环里不够 → 取完剩下的，其余用**上一个采样值**填充（保持电平连续，不产生咔哒声）
   */
  read(dst: Float32Array): void {
    const need = dst.length;
    const got = Math.min(need, this.count);
    let dstOff = 0;
    let left = got;
    while (left > 0) {
      const chunk = Math.min(left, CAPACITY - this.readPos);
      dst.set(this.buf.subarray(this.readPos, this.readPos + chunk), dstOff);
      this.readPos = (this.readPos + chunk) % CAPACITY;
      dstOff += chunk;
      left -= chunk;
    }
    this.count -= got;

    if (got > 0) {
      this.lastSample = dst[dstOff - 1];
    }
    /* 供给不足：延续上一个采样，而不是填 0 */
    if (got < need) {
      dst.fill(this.lastSample, got);
    }
  }

  /**
   * 积压超过总容量时丢最旧的采样。
   *
   * fceux 不会走到这里（写不下时 write 直接返回 0，生产者自己等）；
   * 我们的生产者在主循环里**不等**，一旦核心积压被全量抽干塞进来，就必须靠这里限深。
   */
  trim(): void {
    if (this.count > CAPACITY) {
      const drop = this.count - CAPACITY;
      this.readPos = (this.readPos + drop) % CAPACITY;
      this.count = CAPACITY;
    }
  }

  /** 清空，对应 fceux 的 wo_reset()。 */
  reset(): void {
    this.writePos = 0;
    this.readPos = 0;
    this.count = 0;
    this.lastSample = 0;
    this.buf.fill(0);
  }
}

export { CAPACITY as AUDIO_RING_CAPACITY, SAMPLES_PER_BLOCK as AUDIO_SAMPLES_PER_BLOCK };
