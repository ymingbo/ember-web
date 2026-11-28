import { useCallback, useEffect, useRef, useState } from 'react';
import type {
  EmberWasmInstance,
  EmberWasmModule,
  EmulatorState,
  FloppyDriveState,
  PadButton,
  PrinterPageInfo,
} from '../types/ember';
import { PadButtons } from '../types/ember';
import { AudioRing } from '../audio/AudioRing';

// 动态导入 WASM 模块
let wasmModulePromise: Promise<EmberWasmModule> | null = null;

async function loadWasmModule(): Promise<EmberWasmModule> {
  if (wasmModulePromise) return wasmModulePromise;

  /* .wasm 和 .data 放在 public/wasm/，不是胶水代码所在的 /assets/。
     Vite 会把胶水代码打成 /assets/ember-wasm-<hash>.js，emscripten 默认按
     import.meta.url 找同目录下的 ember_wasm.wasm / .data —— 那里没有这两个文件，
     于是 preload 的 .data 下载静默失败，工厂 promise 永远不 resolve，
     表现就是界面一直停在「加载中」、没有任何报错。

     所以必须显式给 locateFile，把两个文件名都映射到 /wasm/ 下。 */
  /* 相对路径：胶水代码打成 assets/ember-wasm-<hash>.js 后，
     './wasm/' 相对的是页面所在目录而不是脚本目录，所以要用 document.baseURI
     拼出完整地址。这样根路径和任意子目录部署都能找到文件。 */
  const wasmDir = new URL('wasm/', document.baseURI).href;

  wasmModulePromise = import('../ember/ember_wasm.js').then((mod) =>
    mod.default({
      locateFile: (path: string) =>
        path.endsWith('.wasm') || path.endsWith('.data') ? `${wasmDir}${path}` : path,
    }),
  );
  return wasmModulePromise;
}

/**
 * 获取全局 Module 对象（用于访问 WASM 内存视图）。
 */
function getModule(): EmberWasmModule | null {
  return (window as any).EmberWasmModule as EmberWasmModule | null;
}

/**
 * 模拟器 Hook：管理 WASM 实例、运行循环、音频和全部外设输入。
 * v3：完整外设支持（键盘、鼠标、软驱、打印机、磁带机）
 */
export function useEmulator() {
  const instanceRef = useRef<EmberWasmInstance | null>(null);
  const animationFrameRef = useRef<number | null>(null);
  const audioContextRef = useRef<AudioContext | null>(null);
  /* 音频环形缓冲：核心产多少灌多少，ScriptProcessor 从环里取。
     见 AudioRing.ts —— 参数照抄 fceux waveout.c 的 1800×32。 */
  const audioRingRef = useRef<AudioRing | null>(null);
  const audioBufferRef = useRef<Float32Array | null>(null);
  const audioBufferAddrRef = useRef<number>(0);
  const scriptProcessorRef = useRef<ScriptProcessorNode | null>(null);
  const gainNodeRef = useRef<GainNode | null>(null);

  const [state, setState] = useState<EmulatorState>({
    status: 'idle',
    error: null,
    fps: 0,
    frameCount: 0,
    romName: null,
    isLearningMachine: false,
    hasMouse: false,
    floppyDriveCount: 0,
    hasPrinter: false,
    hasTape: false,
  });

  const controller1Ref = useRef<number>(0);
  const controller2Ref = useRef<number>(0);
  const fpsCounterRef = useRef({ frames: 0, lastTime: performance.now(), currentFps: 0 });
  /** 机型场频（Hz）。loadRom 后从核心读，主循环按它节流。 */
  /* 机型原生场频（ROM 真实跑多快），由 loadRom 读 instance.getFps() 写入。
     裕兴/步步高/小霸王都是 PAL 50.0070Hz。 */
  const nativeFpsRef = useRef(0);
  /* 帧率模式：'native' 按机型原生场频节流（PAL 机型 50Hz）；
     '60' 强制锁 60Hz —— 用于适配 60Hz 显示器，代价是 PAL ROM 会快 20%。 */
  const [frameRateMode, setFrameRateMode] = useState<'native' | '60'>('native');
  const frameRateModeRef = useRef<'native' | '60'>('native');
  /* 主循环实际使用的目标帧率 */
  const frameRateRef = useRef(0);
  /** 上次出帧的时间戳，用于按场频对齐。 */
  const lastTickRef = useRef(0);

  // 软驱状态缓存
  const floppyStatesRef = useRef<FloppyDriveState[]>([]);
  const [floppyStates, setFloppyStates] = useState<FloppyDriveState[]>([]);
  /**
   * 各软驱马达当前是否在转。指示灯要跟着读盘闪，走 rAF 直读这里，
   * 不经过 setState —— 否则每帧一次 React 重渲染，整机都会被拖慢。
   */
  const floppyMotorRef = useRef<boolean[]>([]);

  // 打印机页数缓存（用于检测新页）
  const lastPrinterPagesRef = useRef<number>(0);
  const [printerPages, setPrinterPages] = useState<number>(0);

  // 磁带状态
  const [tapeLoaded, setTapeLoaded] = useState(false);
  const [tapePlaying, setTapePlaying] = useState(false);

  // ------------------------------------------------------------------
  // 刷新外设状态（ROM 加载后调用）
  // ------------------------------------------------------------------
  const refreshPeripheralStatus = useCallback(() => {
    const instance = instanceRef.current;
    if (!instance) return;

    const isLearningMachine = instance.hasKeyboard();
    const hasMouse = instance.hasMouse();
    const driveCount = instance.floppyDriveCount();
    const hasPrinter = instance.hasPrinter();
    const hasTape = instance.hasTape();

    setState((s) => ({
      ...s,
      isLearningMachine,
      hasMouse,
      floppyDriveCount: driveCount,
      hasPrinter,
      hasTape,
    }));

    // 初始化软驱状态数组
    const drives: FloppyDriveState[] = [];
    for (let i = 0; i < driveCount; i++) {
      drives.push({
        index: i,
        inserted: false,
        modified: false,
        name: '',
        motor: false,
        writeProtected: false,
        size: 0,
      });
    }
    floppyStatesRef.current = drives;
    setFloppyStates(drives);
  }, []);

  // ------------------------------------------------------------------
  // 音频初始化
  // ------------------------------------------------------------------
  const initAudio = useCallback((sampleRate: number) => {
    if (audioContextRef.current) return;

    const ctx = new AudioContext({ sampleRate, latencyHint: 'interactive' });
    audioContextRef.current = ctx;
    /* 环形缓冲在这里建一次，之后所有回调都从它取数 */
    audioRingRef.current = new AudioRing();

    // 2048 样本缓冲，单声道输入输出
    const processor = ctx.createScriptProcessor(2048, 0, 1);
    scriptProcessorRef.current = processor;

    const gain = ctx.createGain();
    gain.gain.value = 0.5;
    gainNodeRef.current = gain;

    /* 回调只从环形缓冲取数，**不再直接问核心要**。
       原来这里每次硬要 2048 个，核心给不够就 output.fill(0) 补静音 ——
       少一个采样补一个零，就是断续的来源（见 AudioRing.ts 顶部的说明）。
       现在供给与消费解耦：主循环往环里灌，回调从环里取。 */
    processor.onaudioprocess = (e) => {
      const output = e.outputBuffer.getChannelData(0);
      const ring = audioRingRef.current;
      if (!ring) {
        output.fill(0);
        return;
      }
      ring.read(output);
    };

    processor.connect(gain);
    gain.connect(ctx.destination);
  }, []);

  // ------------------------------------------------------------------
  // 主循环
  // ------------------------------------------------------------------
  /* 按帧率模式算出主循环的目标帧率。'native' 用机型场频（PAL 50Hz），
     '60' 忽略机型差异强制 60Hz。切换时重置计时基准，避免残留的
     lastTick 让第一帧被误判成"还没到时间"而卡一下。 */
  const applyFrameRateMode = useCallback((mode: 'native' | '60') => {
    frameRateModeRef.current = mode;
    const native = nativeFpsRef.current;
    frameRateRef.current = mode === '60' ? 60 : native > 1 ? native : 60;
    lastTickRef.current = performance.now();
  }, []);

  /* 用户切换帧率模式：改 state 触发重渲染（按钮文案要跟着变），
     同时立刻改主循环的目标帧率，不用等下一次 loadRom。 */
  const changeFrameRateMode = useCallback(
    (mode: 'native' | '60') => {
      setFrameRateMode(mode);
      applyFrameRateMode(mode);
    },
    [applyFrameRateMode],
  );

  const runLoop = useCallback(() => {
    const instance = instanceRef.current;
    if (!instance || !instance.isRunning()) {
      animationFrameRef.current = null;
      return;
    }

    /* 按目标帧率节流。requestAnimationFrame 是 60Hz 固定节拍，
       而裕兴/步步高/小霸王都是 PAL 50.0070Hz —— 不节流就会比真机快 20%，
       表现为整体节奏偏快、音乐音调偏高。这里按帧周期累加决定本帧跑不跑，
       不足一帧的时间留到下一个 rAF。

       目标帧率由 frameRateRef 决定：'native' 模式 = 机型场频（PAL 50Hz），
       '60' 模式 = 强制 60Hz（PAL ROM 会快 20%，见 applyFrameRateMode）。 */
    const targetFps = frameRateRef.current > 1 ? frameRateRef.current : 60;
    const frameMs = 1000 / targetFps;
    const nowTick = performance.now();
    const elapsed = nowTick - lastTickRef.current;
    if (elapsed < frameMs) {
      // 还没到下一帧的时间，跳过本次，不做任何计算
      animationFrameRef.current = requestAnimationFrame(runLoop);
      return;
    }
    // 把实际间隔按帧周期对齐，避免误差累积导致越跑越偏
    lastTickRef.current = nowTick - (elapsed % frameMs);

    // 运行一帧
    const frameNum = instance.runFrame();

    /* 每帧把核心产出的音频灌进环形缓冲。
       核心按机型场频产音频（裕兴 50.007Hz），一帧约 960 个采样（48000/50）。
       以前没有这一步——音频只在 ScriptProcessor 回调里现问现取，取不齐就补零。
       现在灌与取解耦：这里算多少灌多少，回调那边从环里平滑取。 */
    {
      const ring = audioRingRef.current;
      const addr = audioBufferAddrRef.current;
      const Module = getModule();
      if (ring && addr && Module) {
        /* 一次性把核心积压全取走（核心侧 takeAudio 就是全量交换），
           先取到 wasm 外的临时数组再灌进环。缓冲按 1 秒采样分配，够用。 */
        const tmp = audioBufferRef.current;
        if (tmp) {
          const got = instance.getAudioSamples(addr, tmp.length);
          /* 自检读数：确认灌入路径真的在跑、每帧拿到多少采样 */
          (window as unknown as { __audioDiag?: Record<string, number> }).__audioDiag = {
            got,
            holds: ring.holds,
            space: ring.space,
          };
          if (got > 0) {
            const heapF32 = Module.HEAPF32;
            const floatOffset = addr / 4;
            ring.write(heapF32.subarray(floatOffset, floatOffset + got), got);
            /* 积压超过环容量时丢最旧的，防止延迟越滚越大 */
            ring.trim();
          }
        }
      }
    }

    /* 每帧采样软驱马达状态：指示灯要随读盘闪，必须跟帧同粒度。
       只写 ref 不碰 state —— 每帧 setState 会让整棵树重渲染，模拟器直接被拖慢。 */
    {
      const count = instance.floppyDriveCount();
      if (floppyMotorRef.current.length !== count) {
        floppyMotorRef.current = new Array<boolean>(count).fill(false);
      }
      const motors = floppyMotorRef.current;
      for (let i = 0; i < count; i++) motors[i] = instance.floppyMotorRunning(i);
    }

    // FPS 计算
    const fps = fpsCounterRef.current;
    fps.frames++;
    const now = performance.now();
    if (now - fps.lastTime >= 500) {
      fps.currentFps = (fps.frames * 1000) / (now - fps.lastTime);
      fps.frames = 0;
      fps.lastTime = now;

      // 检查软驱状态变化
      const driveCount = instance.floppyDriveCount();
      let floppyChanged = false;
      const newStates = [...floppyStatesRef.current];
      for (let i = 0; i < driveCount; i++) {
        const inserted = instance.floppyInserted(i);
        const modified = instance.floppyModified(i);
        // 写保护可能在界面之外被改（故障注入清全部故障时会取消写保护），
        // 所以每轮都读一次，不能只在插入时记下来。
        const writeProtected = inserted ? instance.floppyWriteProtected(i) : false;
        if (
          newStates[i]?.inserted !== inserted ||
          newStates[i]?.modified !== modified ||
          newStates[i]?.writeProtected !== writeProtected
        ) {
          newStates[i] = { ...newStates[i], index: i, inserted, modified, writeProtected };
          floppyChanged = true;
        }
      }
      if (floppyChanged) {
        floppyStatesRef.current = newStates;
        setFloppyStates(newStates);
      }

      // 检查打印机新页
      const pages = instance.printerPagesOutput();
      if (pages !== lastPrinterPagesRef.current) {
        lastPrinterPagesRef.current = pages;
        setPrinterPages(Number(pages));
      }

      // 检查磁带播放状态
      if (instance.hasTape()) {
        const playing = instance.tapeIsPlaying();
        setTapePlaying(playing);
      }

      setState((s) => ({ ...s, fps: fps.currentFps, frameCount: Number(frameNum) }));
    }

    animationFrameRef.current = requestAnimationFrame(runLoop);
  }, []);

  // ------------------------------------------------------------------
  // 加载 ROM
  // ------------------------------------------------------------------
  /* machine 非空时按机型名直接装载裸 ROM（.bin 学习机固件，无 iNES 头）；
     为空时走原来的 iNES 解析路径（.nes 卡带 / 学习机 ROM）。 */
  const loadRom = useCallback(async (romData: Uint8Array, romName: string, machine?: string) => {
    setState((s) => ({ ...s, status: 'loading', error: null }));

    try {
      // 确保 WASM 模块已加载
      const Module = await loadWasmModule();
      // 挂到全局供帧缓冲读取等使用
      (window as any).EmberWasmModule = Module;

      // 创建实例（如果还没有）
      if (!instanceRef.current) {
        instanceRef.current = new Module.EmberWasm();
      }

      const instance = instanceRef.current;

      // 把 ROM 数据拷贝到 wasm 内存
      const romAddr = instance.allocBuffer(romData.length);
      if (romAddr === 0) {
        setState((s) => ({ ...s, status: 'error', error: 'Failed to allocate ROM buffer' }));
        return false;
      }
      Module.HEAPU8.set(romData, romAddr);

      // 加载 ROM：有 machine 字段说明是裸 ROM，跳过 iNES 解析
      const result = machine
        ? instance.loadRomAsMachine(romAddr, romData.length, machine)
        : instance.loadRom(romAddr, romData.length);
      instance.freeBuffer(romAddr);

      if (result !== 0) {
        const err = instance.getError() || 'Unknown ROM load error';
        setState((s) => ({ ...s, status: 'error', error: err }));
        return false;
      }

      // 初始化音频
      const sampleRate = instance.getAudioSampleRate();
      if (sampleRate > 0) {
        initAudio(sampleRate);
        // 分配 wasm 音频缓冲（1 秒采样）
        if (audioBufferAddrRef.current === 0) {
          audioBufferAddrRef.current = instance.allocBuffer(sampleRate * 4); // float = 4 bytes
        }
        audioBufferRef.current = new Float32Array(sampleRate);
      }

      // 刷新外设状态
      refreshPeripheralStatus();

      // 重置磁带状态
      setTapeLoaded(false);
      setTapePlaying(false);
      lastPrinterPagesRef.current = 0;
      setPrinterPages(0);

      setState((s) => ({
        ...s,
        status: 'ready',
        romName,
        fps: 0,
        frameCount: 0,
      }));
      // 读机型场频（PAL 50 / NTSC 60），主循环按它节流
      const hz = instance.getFps();
      nativeFpsRef.current = hz > 1 ? hz : 60;
      applyFrameRateMode(frameRateModeRef.current);
      lastTickRef.current = performance.now();
      return true;
    } catch (err) {
      const message = err instanceof Error ? err.message : String(err);
      setState((s) => ({ ...s, status: 'error', error: message }));
      return false;
    }
  }, [initAudio, refreshPeripheralStatus]);

  // ------------------------------------------------------------------
  // 电源控制
  // ------------------------------------------------------------------
  const powerOn = useCallback(() => {
    const instance = instanceRef.current;
    if (!instance) return;

    instance.powerOn();
    setState((s) => ({ ...s, status: 'running' }));

    // 启动音频上下文（需要用户交互后才能启动）
    if (audioContextRef.current?.state === 'suspended') {
      audioContextRef.current.resume();
    }

    // 启动主循环
    if (!animationFrameRef.current) {
      fpsCounterRef.current = { frames: 0, lastTime: performance.now(), currentFps: 0 };
      lastTickRef.current = performance.now();
      animationFrameRef.current = requestAnimationFrame(runLoop);
    }
  }, [runLoop]);

  const powerOff = useCallback(() => {
    const instance = instanceRef.current;
    if (!instance) return;

    // 停止主循环
    if (animationFrameRef.current) {
      cancelAnimationFrame(animationFrameRef.current);
      animationFrameRef.current = null;
    }

    instance.powerOff();
    /* 照 fceux 的 wo_reset()：断电时清空音频环，不让旧采样接在新画面后面 */
    audioRingRef.current?.reset();
    setState((s) => ({ ...s, status: 'ready', fps: 0, frameCount: 0 }));
  }, []);

  const reset = useCallback(() => {
    instanceRef.current?.reset();
    /* 同上：复位后旧采样不能残留 */
    audioRingRef.current?.reset();
  }, []);

  const pause = useCallback(() => {
    const instance = instanceRef.current;
    if (!instance || !instance.isRunning()) return;

    instance.pause();
    setState((s) => ({ ...s, status: 'paused' }));

    if (animationFrameRef.current) {
      cancelAnimationFrame(animationFrameRef.current);
      animationFrameRef.current = null;
    }
  }, []);

  const resume = useCallback(() => {
    const instance = instanceRef.current;
    if (!instance) return;

    instance.resume();
    setState((s) => ({ ...s, status: 'running' }));

    if (!animationFrameRef.current) {
      fpsCounterRef.current = { frames: 0, lastTime: performance.now(), currentFps: 0 };
      lastTickRef.current = performance.now();
      animationFrameRef.current = requestAnimationFrame(runLoop);
    }
  }, [runLoop]);

  // ------------------------------------------------------------------
  // 手柄输入
  // ------------------------------------------------------------------
  const setButton = useCallback((port: 1 | 2, button: PadButton, pressed: boolean) => {
    const ref = port === 1 ? controller1Ref : controller2Ref;
    const mask = PadButtons[button];

    if (pressed) {
      ref.current |= mask;
    } else {
      ref.current &= ~mask;
    }

    const instance = instanceRef.current;
    if (instance) {
      if (port === 1) {
        instance.setController1(ref.current);
      } else {
        instance.setController2(ref.current);
      }
    }
  }, []);

  const pressButton = useCallback(
    (port: 1 | 2, button: PadButton) => setButton(port, button, true),
    [setButton],
  );

  const releaseButton = useCallback(
    (port: 1 | 2, button: PadButton) => setButton(port, button, false),
    [setButton],
  );

  // ------------------------------------------------------------------
  // 键盘输入（学习机模式：转发所有按键）
  // ------------------------------------------------------------------
  const sendKey = useCallback((code: string, down: boolean): boolean => {
    const instance = instanceRef.current;
    if (!instance || !instance.hasKeyboard()) return false;
    return instance.sendKey(code, down) === 0;
  }, []);

  // ------------------------------------------------------------------
  // 鼠标输入
  // ------------------------------------------------------------------
  const sendMouse = useCallback((
    kind: number,
    x: number,
    y: number,
    dx: number,
    dy: number,
    buttons: number,
    wheel: number,
  ): boolean => {
    const instance = instanceRef.current;
    if (!instance || !instance.hasMouse()) return false;
    const rc = instance.sendMouse(kind, x, y, dx, dy, buttons, wheel);
    return rc === 0;
  }, []);

  // ------------------------------------------------------------------
  // 软驱操作
  // ------------------------------------------------------------------

  /** 把一段镜像字节插进软驱（文件导入和新建空白盘共用这条路径）。 */
  const insertFloppyData = useCallback(
    (index: number, data: Uint8Array, name: string, writeProtected: boolean): boolean => {
      const instance = instanceRef.current;
      const Module = getModule();
      if (!instance || !Module) return false;

      const addr = instance.allocBuffer(data.length);
      if (addr === 0) return false;

      Module.HEAPU8.set(data, addr);
      const result = instance.floppyInsert(index, addr, data.length, writeProtected, name);
      instance.freeBuffer(addr);

      if (result !== 0) return false;

      const newStates = [...floppyStatesRef.current];
      newStates[index] = {
        index,
        inserted: true,
        modified: false,
        name,
        motor: floppyMotorRef.current[index] === true,
        writeProtected,
        size: data.length,
      };
      floppyStatesRef.current = newStates;
      setFloppyStates(newStates);
      return true;
    },
    [],
  );

  const floppyInsert = useCallback(
    async (index: number, file: File, writeProtected = false): Promise<boolean> => {
      try {
        const arrayBuffer = await file.arrayBuffer();
        return insertFloppyData(index, new Uint8Array(arrayBuffer), file.name, writeProtected);
      } catch {
        return false;
      }
    },
    [insertFloppyData],
  );

  /**
   * 新建一张空白软盘插进驱动器。
   *
   * 镜像就是全零字节 —— 格式由核心按镜像大小反推（见上游 formatFor）：
   * 737280 字节 → 80 磁道 / 双面 / 每道 9 扇区 / 250kbps（720K）
   * 1474560 字节 → 80 磁道 / 双面 / 每道 18 扇区 / 500kbps（1440K）
   * 空白盘不含文件系统，需要在学习机里执行格式化命令后才能存文件。
   */
  const floppyInsertBlank = useCallback(
    (index: number, sizeBytes: number, name: string, writeProtected = false): boolean =>
      insertFloppyData(index, new Uint8Array(sizeBytes), name, writeProtected),
    [insertFloppyData],
  );

  /** 设置或取消盘片写保护；核心的 FDC 会在写数据、格式化时拦截。 */
  const floppySetWriteProtected = useCallback((index: number, protect: boolean): boolean => {
    const instance = instanceRef.current;
    if (!instance) return false;
    const result = instance.floppySetWriteProtected(index, protect);
    if (result === 0) {
      const newStates = [...floppyStatesRef.current];
      if (newStates[index]) {
        newStates[index] = { ...newStates[index], writeProtected: protect };
        floppyStatesRef.current = newStates;
        setFloppyStates(newStates);
      }
      return true;
    }
    return false;
  }, []);

  const floppyEject = useCallback((index: number): boolean => {
    const instance = instanceRef.current;
    if (!instance) return false;

    const result = instance.floppyEject(index);
    if (result === 0) {
      const newStates = [...floppyStatesRef.current];
      newStates[index] = {
        index,
        inserted: false,
        modified: false,
        name: '',
        motor: false,
        writeProtected: false,
        size: 0,
      };
      floppyStatesRef.current = newStates;
      setFloppyStates(newStates);
      return true;
    }
    return false;
  }, []);

  const floppySaveImage = useCallback((index: number): Uint8Array | null => {
    const instance = instanceRef.current;
    const Module = getModule();
    if (!instance || !Module) return null;

    // Embind 不支持引用出参，尺寸从 lastImageSize 属性读
    const addr = instance.floppyGetImage(index);
    const size = instance.lastImageSize;
    if (addr === 0 || size === 0) return null;

    // 从 wasm 内存拷贝出来
    const data = new Uint8Array(size);
    data.set(Module.HEAPU8.subarray(addr, addr + size));
    return data;
  }, []);

  // ------------------------------------------------------------------
  // 打印机
  // ------------------------------------------------------------------
  const getPrinterLastPage = useCallback((): PrinterPageInfo | null => {
    const instance = instanceRef.current;
    const Module = getModule();
    if (!instance || !Module) return null;

    // 尺寸从三个属性读，不走出参
    const addr = instance.printerGetLastPage();
    const size = instance.lastPageSize;
    if (addr === 0 || size === 0) return null;

    const data = new Uint8Array(size);
    data.set(Module.HEAPU8.subarray(addr, addr + size));

    return {
      width: instance.lastPageW,
      height: instance.lastPageH,
      data,
    };
  }, []);

  // ------------------------------------------------------------------
  // 磁带机
  // ------------------------------------------------------------------
  const tapeLoad = useCallback(async (file: File): Promise<boolean> => {
    const instance = instanceRef.current;
    const Module = getModule();
    if (!instance || !Module || !instance.hasTape()) return false;

    try {
      // 使用 Web Audio 解码音频，再重采样到 96kHz 16-bit PCM
      const arrayBuffer = await file.arrayBuffer();
      const audioCtx = new AudioContext({ sampleRate: 96000 });
      const audioBuffer = await audioCtx.decodeAudioData(arrayBuffer.slice(0));

      // 取左声道（单声道），转成 16-bit PCM
      const channelData = audioBuffer.getChannelData(0);
      const pcmData = new Int16Array(channelData.length);
      for (let i = 0; i < channelData.length; i++) {
        const s = Math.max(-1, Math.min(1, channelData[i]));
        pcmData[i] = s < 0 ? s * 0x8000 : s * 0x7fff;
      }

      // 拷贝到 wasm 内存
      const byteLength = pcmData.length * 2;
      const addr = instance.allocBuffer(byteLength);
      if (addr === 0) {
        audioCtx.close();
        return false;
      }

      // Int16Array -> Uint8Array 拷贝
      const srcBytes = new Uint8Array(pcmData.buffer);
      Module.HEAPU8.set(srcBytes, addr);

      const result = instance.tapeLoad(addr, byteLength);
      instance.freeBuffer(addr);
      audioCtx.close();

      if (result === 0) {
        setTapeLoaded(true);
        return true;
      }
      return false;
    } catch {
      return false;
    }
  }, []);

  const tapeRewind = useCallback(() => {
    instanceRef.current?.tapeRewind();
  }, []);

  // ------------------------------------------------------------------
  // 帧缓冲访问（供 Canvas 渲染）
  // ------------------------------------------------------------------
  const getFramebuffer = useCallback((): {
    data: Uint8ClampedArray | null;
    width: number;
    height: number;
  } => {
    const instance = instanceRef.current;
    if (!instance) return { data: null, width: 0, height: 0 };

    const ptr = instance.getFramebufferPtr();
    const width = instance.getFrameWidth();
    const height = instance.getFrameHeight();

    if (!ptr || width === 0 || height === 0) {
      return { data: null, width: 0, height: 0 };
    }

    const Module = getModule();
    if (!Module?.HEAPU8) return { data: null, width: 0, height: 0 };

    const data = new Uint8ClampedArray(
      Module.HEAPU8.buffer,
      ptr,
      width * height * 4,
    );

    return { data, width, height };
  }, []);

  // ------------------------------------------------------------------
  // 音量控制
  // ------------------------------------------------------------------
  const setVolume = useCallback((volume: number) => {
    if (gainNodeRef.current) {
      gainNodeRef.current.gain.value = Math.max(0, Math.min(1, volume));
    }
  }, []);

  // ------------------------------------------------------------------
  // 清理
  // ------------------------------------------------------------------
  useEffect(() => {
    return () => {
      if (animationFrameRef.current) {
        cancelAnimationFrame(animationFrameRef.current);
      }
      if (scriptProcessorRef.current) {
        scriptProcessorRef.current.disconnect();
      }
      if (gainNodeRef.current) {
        gainNodeRef.current.disconnect();
      }
      if (audioContextRef.current) {
        audioContextRef.current.close();
        audioRingRef.current = null;
      }
      if (instanceRef.current) {
        if (audioBufferAddrRef.current) {
          instanceRef.current.freeBuffer(audioBufferAddrRef.current);
        }
        instanceRef.current = null;
      }
    };
  }, []);

  return {
    state,
    loadRom,
    powerOn,
    powerOff,
    reset,
    pause,
    resume,
    pressButton,
    releaseButton,
    getFramebuffer,
    setVolume,
    // ---- 外设 ----
    // 键盘
    sendKey,
    // 鼠标
    sendMouse,
    // 软驱
    floppyStates,
    floppyMotorRef,
    floppyInsert,
    floppyInsertBlank,
    floppySetWriteProtected,
    floppyEject,
    floppySaveImage,
    // 打印机
    printerPages,
    getPrinterLastPage,
    // 磁带机
    tapeLoaded,
    tapePlaying,
    tapeLoad,
    tapeRewind,
    // 帧率模式（'native' 原生场频 / '60' 锁 60Hz）
    frameRateMode,
    setFrameRateMode: changeFrameRateMode,
    // 实例引用（高级用法）
    instance: instanceRef.current,
  };
}
