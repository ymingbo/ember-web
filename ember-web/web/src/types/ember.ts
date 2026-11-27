/**
 * Ember WASM 模块类型定义
 * 对应 wasm_bindings.cpp 中的 EmberWasm 类
 * v3：完整外设支持（键盘、鼠标、软驱、打印机、磁带机）
 */

export interface EmberWasmModule {
  EmberWasm: new () => EmberWasmInstance;
  // 手柄按键
  PAD_A: number;
  PAD_B: number;
  PAD_SELECT: number;
  PAD_START: number;
  PAD_UP: number;
  PAD_DOWN: number;
  PAD_LEFT: number;
  PAD_RIGHT: number;
  // 鼠标事件类型
  MOUSE_MOVE: number;
  MOUSE_DOWN: number;
  MOUSE_UP: number;
  MOUSE_WHEEL: number;
  MOUSE_LEAVE: number;
  // 鼠标按键位
  MOUSE_LEFT: number;
  MOUSE_RIGHT: number;
  MOUSE_MIDDLE: number;
  // WASM 内存视图
  HEAPU8: Uint8Array;
  HEAPF32: Float32Array;
}

export interface EmberWasmInstance {
  // ---- 内存管理 ----
  /** 在 wasm 内存中分配缓冲区，返回地址（0 = 失败） */
  allocBuffer(size: number): number;
  /** 释放 allocBuffer 分配的缓冲区 */
  freeBuffer(address: number): void;

  // ---- ROM 加载 ----
  /** 加载 ROM（数据在 wasm 内存中），返回 0 成功，非 0 失败 */
  loadRom(address: number, size: number): number;
  /**
   * 按机型名装载裸 ROM —— 用于没有 iNES 头的学习机固件（上游 ember/roms/*.bin）。
   * machineName 必须是核心认识的机型名（yuxing/bbg98/bbg1/sb97/sb2000/pec9588/cec1）。
   */
  loadRomAsMachine(address: number, size: number, machineName: string): number;

  // ---- 电源控制 ----
  powerOn(): void;
  powerOff(): void;
  reset(): void;
  pause(): void;
  resume(): void;
  isRunning(): boolean;
  isPoweredOn(): boolean;

  // ---- 帧 ----
  runFrame(): number;
  getFramebufferPtr(): number;
  copyFramebuffer(dst: number, maxBytes: number): number;
  /** 当前帧调色板索引指针（每像素 1 字节，0..511）；不支持时返回 0 */
  getFrameWidth(): number;
  getFrameHeight(): number;

  // ---- 手柄 ----
  setController1(buttons: number): void;
  setController2(buttons: number): void;

  // ---- 音频 ----
  getAudioSamples(dst: number, maxSamples: number): number;
  getAudioSampleRate(): number;
  getFps(): number;

  // ---- 错误 ----
  getError(): string;

  // ====================================================================
  // 键盘（学习机必备）
  // ====================================================================
  /**
   * 发送键盘事件。
   * @param code 物理键位，取 KeyboardEvent.code，如 "KeyA"、"Enter"、"Space"
   * @param down 是否按下
   * @return 0 成功，非 0 失败
   */
  sendKey(code: string, down: boolean): number;
  /** 当前机型是否有内置键盘（学习机为 true，FC 为 false） */
  hasKeyboard(): boolean;

  // ====================================================================
  // 鼠标
  // ====================================================================
  /**
   * 发送鼠标事件。
   * @param kind 事件类型（MOUSE_MOVE/DOWN/UP/WHEEL/LEAVE）
   * @param x 画面 X 坐标（256x240 标准帧像素）
   * @param y 画面 Y 坐标
   * @param dx 相对位移 X
   * @param dy 相对位移 Y
   * @param buttons 当前按住的按键位（MOUSE_LEFT/RIGHT/MIDDLE）
   * @param wheel 滚轮滚动量
   * @return 0 成功，非 0 失败
   */
  sendMouse(
    kind: number,
    x: number,
    y: number,
    dx: number,
    dy: number,
    buttons: number,
    wheel: number,
  ): number;
  /** 当前机型是否支持鼠标 */
  hasMouse(): boolean;

  // ====================================================================
  // 软驱（磁盘）
  // ====================================================================
  /** 软驱数量 */
  floppyDriveCount(): number;
  /**
   * 插入软盘镜像到指定软驱。
   * @param index 软驱编号（从 0 开始）
   * @param dataAddr 镜像数据在 wasm 内存中的地址
   * @param size 镜像大小（字节）
   * @param writeProtected 是否写保护
   * @param name 镜像名称
   * @return 0 成功，非 0 失败
   */
  floppyInsert(
    index: number,
    dataAddr: number,
    size: number,
    writeProtected: boolean,
    name: string,
  ): number;
  /** 弹出指定软驱的盘片，返回 0 成功 */
  floppyEject(index: number): number;
  /** 指定软驱里是否有盘片 */
  floppyInserted(index: number): boolean;
  /** 盘片是否已修改（需要保存） */
  floppyModified(index: number): boolean;
  /**
   * 软驱马达是否在转。真机的读写灯跟马达同亮同灭，界面据此点亮驱动器指示灯。
   */
  floppyMotorRunning(index: number): boolean;
  /**
   * 获取盘片镜像数据（用于保存到本地）。
   * 尺寸写在 lastImageSize 属性里（Embind 不支持引用出参）。
   * @param index 软驱编号
   * @return 镜像数据在 wasm 内存中的地址；没有盘片返回 0
   */
  floppyGetImage(index: number): number;
  /** 上一次 floppyGetImage 的镜像字节数 */
  readonly lastImageSize: number;

  // ====================================================================
  // 打印机
  // ====================================================================
  /** 当前机型是否有打印机 */
  hasPrinter(): boolean;
  /** 自启动以来输出的页数 */
  printerPagesOutput(): number;
  /**
   * 获取最新一页打印输出（位图）。
   * 宽高和尺寸分别写在 lastPageW / lastPageH / lastPageSize 属性里。
   * @return 位图数据在 wasm 内存中的地址（1 位每像素，每行按字节补齐，高位在左）；没有页返回 0
   */
  printerGetLastPage(): number;
  /** 上一次 printerGetLastPage 的页面宽高与字节数 */
  readonly lastPageW: number;
  readonly lastPageH: number;
  readonly lastPageSize: number;

  // ====================================================================
  // 磁带机
  // ====================================================================
  /** 当前机型是否有磁带机接口 */
  hasTape(): boolean;
  /**
   * 载入磁带音频数据（96kHz 16位 PCM）。
   * @param dataAddr 音频数据在 wasm 内存中的地址
   * @param size 数据大小（字节）
   * @return 0 成功，非 0 失败
   */
  tapeLoad(dataAddr: number, size: number): number;
  /** 倒带 */
  tapeRewind(): void;
  /** 磁带是否正在播放 */
  tapeIsPlaying(): boolean;
}

/** 手柄按键掩码 */
export const PadButtons = {
  A: 0x01,
  B: 0x02,
  SELECT: 0x04,
  START: 0x08,
  UP: 0x10,
  DOWN: 0x20,
  LEFT: 0x40,
  RIGHT: 0x80,
} as const;

export type PadButton = keyof typeof PadButtons;

/** 鼠标事件类型 */
export const MouseEventKind = {
  MOVE: 0,
  DOWN: 1,
  UP: 2,
  WHEEL: 3,
  LEAVE: 4,
} as const;

/** 鼠标按键位 */
export const MouseButtons = {
  LEFT: 1,
  RIGHT: 2,
  MIDDLE: 4,
} as const;

/** 模拟器状态 */
export type EmulatorStatus = 'idle' | 'loading' | 'ready' | 'running' | 'paused' | 'error';

/** 模拟器状态信息 */
export interface EmulatorState {
  status: EmulatorStatus;
  error: string | null;
  fps: number;
  frameCount: number;
  romName: string | null;
  /** 是否为学习机模式（有键盘） */
  isLearningMachine: boolean;
  /** 是否支持鼠标 */
  hasMouse: boolean;
  /** 软驱数量 */
  floppyDriveCount: number;
  /** 是否有打印机 */
  hasPrinter: boolean;
  /** 是否有磁带机 */
  hasTape: boolean;
}

/** 软驱状态 */
export interface FloppyDriveState {
  index: number;
  inserted: boolean;
  modified: boolean;
  name: string;
  /** 马达是否在转：真机的读写灯跟马达同亮同灭。 */
  motor: boolean;
}

/** 打印机页信息 */
export interface PrinterPageInfo {
  width: number;
  height: number;
  data: Uint8Array | null;
}
