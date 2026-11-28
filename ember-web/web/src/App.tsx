import { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import { useEmulator } from './hooks/useEmulator';
import { EmulatorCanvas, type RenderMode } from './components/EmulatorCanvas';
import { KeyboardController } from './components/KeyboardController';
import { OnScreenController } from './components/OnScreenController';
import { PeripheralPanel } from './components/PeripheralPanel';
import { VirtualKeyboard, type VirtualKeyboardHandle } from './components/VirtualKeyboard';
import { AboutDialog } from './components/AboutDialog';

/**
 * 把页面高度钉在「可视区」上，而不是 CSS 的 100dvh。
 *
 * 为什么必须用 JS：手机软键盘弹出时，减小的是 **visualViewport**，
 * 而 dvh / 100% 算的是布局视口，各浏览器对 dvh 是否随键盘变化行为不一致
 * （Android Chrome 实测 dvh 不变）。所以纯 CSS 方案在真机上解决不了
 * 「键盘一弹出来模拟器画面被顶出屏幕」。
 *
 * 这里读取 visualViewport.height 写进 --app-h，CSS 里各层高度都用它。
 * 键盘弹出 → visualViewport 变矮 → --app-h 变小 → 整页跟着收缩，
 * 画面始终留在可见区内。
 */
function useViewportHeight() {
  useEffect(() => {
    const vv = window.visualViewport;

    /* 记录「没有软键盘时」的视口高度，作为基准。 */
    let baseHeight = vv ? vv.height : window.innerHeight;
    /* 同步记录基准宽度。键盘弹出只改高度、不改宽度；
       横竖屏旋转两个方向都变。靠这个区分「键盘」和「旋转」。 */
    let baseWidth = vv ? vv.width : window.innerWidth;

    const apply = () => {
      const root = document.documentElement;
      if (!vv) {
        root.style.setProperty('--app-h', window.innerHeight + 'px');
        root.style.setProperty('--app-shift', '0px');
        return;
      }

      const h = vv.height;
      const w = vv.width;

      /* 视口变高（键盘收起/横竖屏切换）时刷新基准；
         键盘弹出时 vv.height 变小，基准不动，只算差值。

         横竖屏旋转会让视口**变矮**（844 → 390），旧写法只看 h >= baseHeight
         就把它当成键盘弹出，baseHeight 卡在竖屏的 844 不更新，--app-h 一直
         写 844px —— body 比视口高一倍，画面下半截溢出屏幕。

         宽度变化是旋转的可靠信号（键盘不改宽度），所以宽度变了就直接重设基准。 */
      const widthChanged = Math.abs(w - baseWidth) > 1;
      if (h >= baseHeight - 1 || widthChanged) {
        baseHeight = h;
        baseWidth = w;
      }

      /* --app-h 保持**基准高度**，页面高度不变 —— 画面不会被压缩，
         用户的画面尺寸也不会因为弹键盘而抖一下。 */
      root.style.setProperty('--app-h', baseHeight + 'px');

      /* --app-shift = 键盘占掉的高度差。
         软键盘弹出时，可见区从底部被吃掉一截，而 body 仍按基准高度排版，
         结果是下半部分藏到键盘后面、顶栏反而被推出视野。
         把整个内容上移这个差值，顶栏就重新回到可见区顶部。
         vv.offsetTop 是浏览器自己滚动的量，一并计入以免重复平移。 */
      const shift = Math.max(0, baseHeight - h - (vv.offsetTop || 0));
      root.style.setProperty('--app-shift', shift + 'px');
    };

    apply();
    vv?.addEventListener('resize', apply);
    /* 键盘弹出/收起在部分 Android 上只触发 scroll 不触发 resize */
    vv?.addEventListener('scroll', apply);
    window.addEventListener('resize', apply);
    window.addEventListener('orientationchange', apply);
    return () => {
      vv?.removeEventListener('resize', apply);
      vv?.removeEventListener('scroll', apply);
      window.removeEventListener('resize', apply);
      window.removeEventListener('orientationchange', apply);
    };
  }, []);
}

/* ------------------------------------------------------------------ 版本号

   两套版本号，用途不同，不要混用：

     RELEASE_VERSION  对外发布版本号。**只在正式推送到仓库 / 发 Release 时 +1**，
                      跟着 git tag 走，也是界面上显示的那个。
     WORK_VERSION     工作区迭代号。每打包一次给用户的内部测试版就 +1，
                      只在本地和给用户的测试包说明里用，不上界面、不推仓库。

   推送仓库时只改 RELEASE_VERSION 一处；WORK_VERSION 继续往上累加，
   用来区分「给用户测的第几版」，与仓库 tag 互不干扰。 */

/** 对外发布版本号 —— 跟 git tag 一致，界面显示的就是这个。只在推仓库时改。 */
const RELEASE_VERSION: string = '1.0.3';

/** 工作区迭代号 —— 每次打包给用户测就 +1。不显示在界面上。
    导出是为了让打包脚本（package.py）能读出来写进使用说明，
    界面上不引用它，用户看到的永远是发布号。

    沿用原开发工作区那条线（2.53.x → 2.59.0）的编号格式，不从 1.0.x 重起，
    这样内部迭代的历史是连续的，不会出现两套号对不上。 */
export const WORK_VERSION: string = '2.60.1';

/**
 * 内置机型 ROM —— 点一下直接加载开机。
 *
 * 文件放在 public/roms/，构建时原样复制到 dist/roms/，运行时按相对路径取，
 * 所以部署到任意子目录都能用（不能写绝对路径 /roms/，那样子目录会 404）。
 *
 * 机型不是按钮上写死的，而是由核心按 mapper 号 + PRG 大小自动分流
 * （见 ember_web_api.cpp 的 detectMachineType）：
 *   mapper 169 + PRG ≥1MB   → yuxing    裕兴
 *   mapper 168 + PRG ≥1MB   → sb97      小霸王 SB-97
 *   mapper 171 + PRG ≤128K  → bbg1      步步高 软驱一号
 *   mapper 171 + PRG ≤512K  → sb2000    小霸王 SB-2000
 *   mapper 171 + PRG >512K  → bbg98     步步高 98 型
 *   mapper 179 + PRG ≤512K  → pec9588   金字塔
 *   其余                    → fc        普通 FC 卡带
 *
 * 金字塔 PEC-586 与 PEC-686F 两个 ROM 共用 pec9588 这一个机型（上游也是这么分的），
 * 所以这里只摆一份。想换 ROM 直接选自定义 ROM 加载即可。
 */
/** 把相对路径解析成绝对 URL —— base 是 './' 时也能正确取到文件。 */
function resolveAsset(rel: string): string {
  return new URL(rel, document.baseURI).href;
}

/**
 * 机型清单类型。每一条对应 roms/ 目录里的一个 ROM 文件。
 *
 * 机型由核心按 mapper 号 + PRG 大小自动分流（见 ember_web_api.cpp 的
 * detectMachineType），所以这里不需要写机型名，只写给人看的标签和文件名。
 */
export interface MachineEntry {
  /** ROM 文件名，位于 roms/ 目录下 */
  file: string;
  /** 下拉框里显示的名字 */
  label: string;
  /** 分组名，例如「学习机」。同组会归到一个 optgroup 里 */
  group: string;
  /**
   * 机型名（yuxing / bbg98 / bbg1 / sb97 / sb2000 / pec9588 / cec1）。
   * 填了表示这是**裸 ROM**（上游 ember/roms/*.bin，无 iNES 头），
   * 直接交给该机型，跳过 iNES 解析；不填则按 .nes 卡带正常解析。
   */
  machine?: string;
  /** 鼠标悬停提示，可省略 */
  desc?: string;
}

/**
 * 落底清单 —— roms/machines.json 取不到时用它，保证下拉框不会是空的。
 *
 * 正常情况下机型由 machines.json 决定：以后要加内置机型，把 ROM 放进 roms/
 * 再往 machines.json 里加一行就行，不用改这里、也不用重新构建前端。
 */
const FALLBACK_ROMS: MachineEntry[] = [
  { file: 'yuxing.nes', label: '裕兴 V9.2 · 98型卡', group: '学习机', desc: '裕兴学习机' },
  { file: 'bbg98.nes', label: '步步高 98 型', group: '学习机', desc: '步步高 98 型' },
  { file: 'bbg1.nes', label: '步步高 软驱一号', group: '学习机', desc: '步步高 软驱一号（128K 版）' },
  { file: 'xiaobawang.nes', label: '小霸王 SB-97', group: '学习机', desc: '小霸王 SB-97' },
  { file: 'sb2000.nes', label: '小霸王 SB-2000', group: '学习机', desc: '小霸王 SB-2000（512K 版）' },
  { file: 'pec9588.nes', label: '金字塔 PEC-9588', group: '学习机', desc: '金字塔 PEC-9588（PEC-586 / 686F 同机型）' },
];

/** 内置 ROM 所在目录（相对 dist 根，子目录部署也能取到）。 */
const ROM_DIR = 'roms';
/** 机型清单文件的相对位置（跟 roms/ 同目录，方便一起拷贝）。 */
const MACHINE_MANIFEST = 'roms/machines.json';

/**
 * 读机型清单。文件不存在、格式不对、字段缺失都退回 FALLBACK_ROMS，
 * 不让一个坏 JSON 把整个界面卡死。
 */
async function loadMachineManifest(): Promise<MachineEntry[]> {
  try {
    const res = await fetch(resolveAsset(MACHINE_MANIFEST), { cache: 'no-cache' });
    if (!res.ok) return FALLBACK_ROMS;
    const raw: unknown = await res.json();
    if (!Array.isArray(raw)) return FALLBACK_ROMS;
    const cleaned = raw
      .filter((it): it is Record<string, unknown> => !!it && typeof it === 'object')
      .filter((it) => typeof it.file === 'string' && it.file.length > 0 && typeof it.label === 'string')
      .map((it) => ({
        file: it.file as string,
        label: it.label as string,
        group: typeof it.group === 'string' && it.group ? (it.group as string) : '学习机',
        desc: typeof it.desc === 'string' ? (it.desc as string) : undefined,
        machine: typeof it.machine === 'string' && it.machine ? (it.machine as string) : undefined,
      }));
    return cleaned.length > 0 ? cleaned : FALLBACK_ROMS;
  } catch {
    // 离线打开、子目录部署取不到、JSON 坏了 —— 都走兜底
    return FALLBACK_ROMS;
  }
}

/** 下拉菜单里代表"浏览本地 ROM 文件"的选项值 —— 和机型 id 区分开。 */
const OPTION_BROWSE = '__browse__';
/** 下拉菜单里代表"经典 FC 卡带"的选项值 —— 不加载内置 ROM，只切机型等用户插卡。 */
const OPTION_FC = '__fc__';

/** 状态机的中文说明。 */
const STATUS_TEXT: Record<string, string> = {
  idle: '待机',
  loading: '加载中',
  ready: '就绪',
  running: '运行中',
  paused: '已暂停',
  error: '出错',
};

export default function App() {
  /* 页面高度跟随可视区（软键盘弹出时收缩），必须最先执行 */
  useViewportHeight();

  const {
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
    frameRateMode,
    setFrameRateMode,
    setVolume,
    sendKey,
    sendMouse,
    floppyStates,
    floppyMotorRef,
    floppyInsert,
    floppyInsertBlank,
    floppySetWriteProtected,
    floppyEject,
    floppySaveImage,
    printerPages,
    getPrinterLastPage,
    tapeLoaded,
    tapePlaying,
    tapeLoad,
    tapeRewind,
  } = useEmulator();

  const [renderMode, setRenderMode] = useState<RenderMode>('webgl');
  /* 画面平滑：默认开启双线性插值。
     照 ember win32 的 display.filter 设置（screen-window.cpp:326），
     但那边默认是 pixel。这里默认 smooth —— 关掉锐化和玻璃反光后
     画面偏硬，双线性能补一点柔和感，观感更接近 CRT。
     这一项只作用于 3D 电视模式，Canvas2D 走 putImageData 不经过着色器。 */
  const smooth = true;
  const [modeInfo, setModeInfo] = useState('');
  const [renderError, setRenderError] = useState<string | null>(null);
  const [volume, setVolumeState] = useState(0.5);
  /* 触屏手柄：**默认关闭**，手机端也一样，由用户在侧栏开关里自己打开。
     曾按 pointer:coarse 在手机上默认打开，用户明确要求改回默认关。 */
  const [showTouchControls, setShowTouchControls] = useState(false);
  /* 虚拟键盘开关：学习机从启动起随时可能要打字，等系统自动弹键盘
     不可能（画布不是可输入元素），所以做成和手柄一样的显式开关。 */
  const [showVirtualKeyboard, setShowVirtualKeyboard] = useState(false);
  const [showAbout, setShowAbout] = useState(false);

  /* 是否需要虚拟键盘（= 触屏设备或窄窗口）。
     桌面浏览器有实体键盘，顶栏那个「键盘」按钮纯属多余，隐藏掉。
     判断用「触点能力 or 窗口宽度」，不用 CSS 的 pointer 媒体特性 ——
     无头浏览器和部分安卓 WebView 里 pointer: fine / coarse 双双不匹配，
     靠它判断会失效。 */
  const [kbCapable, setKbCapable] = useState(false);
  useEffect(() => {
    const compute = () => {
      const touch =
        (navigator.maxTouchPoints ?? 0) > 0 ||
        (typeof window.matchMedia === 'function' && window.matchMedia('(pointer: coarse)').matches);
      const narrow = window.innerWidth < 820;
      const on = touch || narrow;
      setKbCapable(on);
      /* 同步给 CSS，用来隐藏按钮 —— 首帧渲染前就能生效，避免闪一下 */
      document.documentElement.dataset.kbCapable = on ? '1' : '0';
    };
    compute();
    window.addEventListener('resize', compute);
    window.addEventListener('orientationchange', compute);
    return () => {
      window.removeEventListener('resize', compute);
      window.removeEventListener('orientationchange', compute);
    };
  }, []);

  /* 虚拟键盘锚点的引用。打开开关时组件自己会 focus，
     这里留着是为了将来需要外部唤起时可以直接调。 */
  const vkbRef = useRef<VirtualKeyboardHandle | null>(null);

  /* 把底部栏栈的实测高度写进根节点 --stack-h。
     .drawer 是 position:fixed 的独立层，继承不到 .stage 上的变量，
     只能写在根节点。用 ResizeObserver 而不是 effect 里读一次：
     开/关手柄、开/关键盘栏都会改变栈高，必须跟着更新。

     以前的写法是把每条栏的高度硬编码再 calc 相加，多一条栏就错。
     实测高度对任意组合都成立。 */
  const stackRef = useRef<HTMLDivElement | null>(null);
  useEffect(() => {
    const el = stackRef.current;
    if (!el) return;
    const apply = () => {
      const h = el.getBoundingClientRect().height;
      document.documentElement.style.setProperty('--stack-h', h + 'px');
    };
    apply();
    const ro = new ResizeObserver(apply);
    ro.observe(el);
    return () => ro.disconnect();
  }, []);

  /* 面板展开高度的自适应上限。

     量出「电视机下沿 → 把手上沿」之间的空隙：
       · 空隙宽裕 → 面板只长到空隙里，不遮住电视机
       · 空隙紧张（宽屏横屏、画面很高）→ 不设限，按老逻辑向上覆盖画面

     判断必须在布局完成、面板内容渲染之后做 —— 面板高度取决于机型外设项数，
     所以用 ResizeObserver 跟画面区和内容区，窗口尺寸/方向变了也重算。 */
  const panelBodyRef = useRef<HTMLDivElement | null>(null);
  const screenAreaRef = useRef<HTMLElement | null>(null);
  useEffect(() => {
    const area = screenAreaRef.current;
    const body = panelBodyRef.current;
    if (!area || !body) return;

    /* 一行约 44px（面板里最小的按钮/开关行）。不到 3 行说明空隙太小，
       硬塞进去只会挤成一坨，不如让它盖住画面。 */
    const MIN_USEFUL = 44 * 3;

    const apply = () => {
      const canvas = area.querySelector('canvas');
      const handle = document.querySelector('.drawer-handle');
      if (!canvas || !handle) return;

      const cRect = canvas.getBoundingClientRect();
      const hRect = handle.getBoundingClientRect();

      /* 画面下沿 → 把手上沿，就是可以安全占用的空隙 */
      const gap = hRect.top - cRect.bottom;
      /* 面板内容自然高度，上限仍是 62vh */
      const need = Math.min(body.scrollHeight, Math.round(window.innerHeight * 0.62));

      if (gap >= MIN_USEFUL) {
        /* 空隙宽裕：面板最多长到空隙那么高，不碰画面 */
        const limit = Math.min(gap, need);
        document.documentElement.style.setProperty('--panel-max-h', Math.floor(limit) + 'px');
        document.documentElement.dataset.panelMode = 'gap';
      } else {
        /* 空隙紧张：撤掉限制，CSS 退回 62vh，允许向上覆盖画面 */
        document.documentElement.style.removeProperty('--panel-max-h');
        document.documentElement.dataset.panelMode = 'overlay';
      }
    };

    apply();
    const ro = new ResizeObserver(apply);
    ro.observe(area);
    ro.observe(body);
    window.addEventListener('resize', apply);
    window.addEventListener('orientationchange', apply);
    return () => {
      ro.disconnect();
      window.removeEventListener('resize', apply);
      window.removeEventListener('orientationchange', apply);
    };
  }, []);

  /* 窄屏时侧栏是抽屉，默认**展开**——否则用户看不到「选择 ROM」按钮，
     会以为界面缺东西。桌面端这个状态无影响（侧栏本来常驻）。
     手柄默认关闭，所以面板不需要为它让位。 */
  const [drawerOpen, setDrawerOpen] = useState(true);
  /* 正在加载的机型 id，用来在下拉框旁边转「加载中…」 */
  const [bootingId, setBootingId] = useState<string | null>(null);
  /* 下拉框当前选中的值。选完内置机型后保持选中，选中「浏览本地 ROM…」后自动退回。 */
  const [selectedMachine, setSelectedMachine] = useState<string>(OPTION_FC);
  /* 「浏览本地 ROM…」需要一个能由代码点开的 input，用 ref 直接触发 */
  const romFileRef = useRef<HTMLInputElement | null>(null);
  /* 机型清单。先放兜底值保证首屏就有菜单，再异步换成 roms/machines.json 的内容。 */
  const [machines, setMachines] = useState<MachineEntry[]>(FALLBACK_ROMS);
  useEffect(() => {
    let alive = true;
    void loadMachineManifest().then((list) => {
      if (alive) setMachines(list);
    });
    return () => {
      alive = false;
    };
  }, []);
  /* 下拉框的分组列表，按清单里首次出现的顺序排（不排序，让 machines.json 决定顺序）。 */
  const machineGroups = useMemo(() => {
    const seen: string[] = [];
    for (const m of machines) if (!seen.includes(m.group)) seen.push(m.group);
    return seen;
  }, [machines]);

  const handleLoadRom = useCallback(
    async (file: File) => {
      const arrayBuffer = await file.arrayBuffer();
      await loadRom(new Uint8Array(arrayBuffer), file.name);
    },
    [loadRom],
  );

  /* 点内置机型：取 ROM → 加载 → 自动开机。三步一气呵成，
     用户不需要再单独点「开机」。 */
  const handleBuiltin = useCallback(
    async (item: MachineEntry) => {
      try {
        setBootingId(item.file);
        const res = await fetch(resolveAsset(`${ROM_DIR}/${item.file}`));
        if (!res.ok) throw new Error(`取 ROM 失败 HTTP ${res.status}`);
        const buf = new Uint8Array(await res.arrayBuffer());
        const ok = await loadRom(buf, item.label, item.machine);
        // powerOn 是同步的，loadRom 成功就直接开机
        if (ok) powerOn();
      } catch (e) {
        // 错误对象不能直接拼字符串，取 message
        setRenderError(`加载「${item.label}」失败：${(e as Error).message}`);
      } finally {
        setBootingId(null);
      }
    },
    [loadRom, powerOn],
  );

  /* 下拉框选择：内置机型 → 加载开机；经典 FC → 只切机型不动 ROM；
     浏览本地 ROM → 弹文件选择框。 */
  const handleMachinePick = useCallback(
    (value: string) => {
      if (value === OPTION_BROWSE) {
        romFileRef.current?.click();
        return;
      }
      setSelectedMachine(value);
      if (value === OPTION_FC) return;
      const item = machines.find((m) => m.file === value);
      if (item) void handleBuiltin(item);
    },
    // machines 是异步换进来的，必须进依赖数组，否则闭包一直用兜底清单
    [handleBuiltin, machines],
  );

  const handleVolumeChange = useCallback(
    (v: number) => {
      setVolumeState(v);
      setVolume(v);
    },
    [setVolume],
  );

  const handleModeInfo = useCallback((m: RenderMode, info: string) => {
    // WebGL 起不来时会回报 canvas2d，把界面上的选择同步过去
    setRenderMode((cur) => (m !== cur ? m : cur));
    setModeInfo(info);
  }, []);

  const handleModeError = useCallback((msg: string) => setRenderError(msg), []);

  const hasAnyPeripheral = useMemo(
    () => state.floppyDriveCount > 0 || state.hasPrinter || state.hasTape || state.hasMouse,
    [state.floppyDriveCount, state.hasPrinter, state.hasTape, state.hasMouse],
  );

  const running = state.status === 'running' || state.status === 'paused';

  return (
    <div className="app">
      <header className="topbar">
        <div className="brand">
          <img className="brand-mark" src="./brand-mark.png" alt="" />
          <span className="brand-name">Ember FC</span>
          <span className="brand-ver">v{RELEASE_VERSION}</span>
          {/* 学习机标记：宽屏显示文字，窄屏只留图标（CSS 切换）。
              手机上顶栏空间不够，文字会把它挤到换行。 */}
          {state.isLearningMachine && (
            <span className="tag-learn" title="学习机">
              <span className="lbl-full">学习机</span>
              <span className="lbl-icon" aria-hidden="true">
                ⌨
              </span>
            </span>
          )}
        </div>
        <div className="topbar-right">
          <span className={`status status-${state.status}`} title={STATUS_TEXT[state.status] ?? state.status}>
            <span className="lbl-full">{STATUS_TEXT[state.status] ?? state.status}</span>
            {/* 窄屏用圆点表示状态，颜色已由 status-* 的配色区分 */}
            <span className="lbl-icon status-dot" aria-hidden="true" />
          </span>
          {running && <span className="fps">{state.fps.toFixed(1)} fps</span>}
          {modeInfo && (
            <span className="mode-badge" title={modeInfo}>
              <span className="lbl-full">{modeInfo}</span>
              <span className="lbl-icon" aria-hidden="true">
                {/* 3D 电视模型 → 电视图标；平面 → 方块图标 */}
                {modeInfo.includes('3D') ? '▣' : '▢'}
              </span>
            </span>
          )}
          {/* 虚拟键盘开关：只在学习机机型出现（FC 卡带没有键盘）。
              放在顶栏而不是底部——底部会占掉画面高度，正是「键盘把画面
              顶出屏幕」的来源。

              只在需要软键盘的设备上显示：桌面浏览器有实体键盘，这个按钮
              纯属多余。判断用「触点数 or 窄窗口」——纯 CSS 的 pointer 特性
              在部分环境（无头浏览器、某些安卓 WebView）两个值都不匹配，
              靠它隐藏会失效。 */}
          {state.isLearningMachine && running && kbCapable && (
            <button
              type="button"
              className={`topbar-kb ${showVirtualKeyboard ? 'on' : ''}`}
              title={showVirtualKeyboard ? '收起键盘' : '展开键盘'}
              onClick={() => setShowVirtualKeyboard((v) => !v)}
            >
              {showVirtualKeyboard ? '⌨ 收起' : '⌨ 键盘'}
            </button>
          )}
          <button
            type="button"
            className="topbar-about"
            title="关于本软件"
            onClick={() => setShowAbout(true)}
          >
            关于
          </button>
        </div>
      </header>

      {state.error && (
        <div className="banner banner-error">
          <strong>错误</strong> {state.error}
        </div>
      )}
      {renderError && (
        <div className="banner banner-warn">
          <strong>渲染</strong> {renderError}
        </div>
      )}

      <main className="stage">
        <section id="screen-area" className="screen-area" ref={screenAreaRef}>
          <EmulatorCanvas
            getFramebuffer={getFramebuffer}
            width={256}
            height={240}
            mouseEnabled={state.hasMouse}
            onMouseEvent={sendMouse}
            mode={renderMode}
            onModeInfo={handleModeInfo}
            onModeError={handleModeError}
            powered={running}
            smooth={smooth}
          />
        </section>

        {/* 窄屏下栏：把手 + 面板合成一列贴底。
            把手必须在 aside 外面 —— 放里面会跟着面板一起滑出屏幕，
            收起后就再也点不到，没法把面板拉回来。 */}
        <div className={drawerOpen ? 'drawer open' : 'drawer'}>
        <aside
          className={drawerOpen ? 'sidebar open' : 'sidebar'}
        >
          <div className="sidebar-body" ref={panelBodyRef}>
          <div className="panel">
            <h3 className="panel-title">渲染</h3>
            <div className="panel-row">
              <button
                className="btn btn-compact"
                onClick={() => setRenderMode((m) => (m === 'webgl' ? 'canvas2d' : 'webgl'))}
                title="在 3D 电视模型与平面画面之间切换"
              >
                {renderMode === 'webgl' ? '3D 电视' : 'Canvas2D'}
              </button>
              <button
                className="btn btn-compact"
                onClick={() => setFrameRateMode(frameRateMode === '60' ? 'native' : '60')}
                title={
                  frameRateMode === '60'
                    ? '当前锁 60Hz，PAL 机型（裕兴/步步高/小霸王）会比真机快 20%'
                    : '当前按机型原生场频运行（PAL 50Hz）。点一下锁 60Hz，画面更顺滑，但 ROM 会快 20%'
                }
              >
                {frameRateMode === '60' ? '60Hz' : '50Hz'}
              </button>
            </div>
          </div>

          <div className="panel">
            <h3 className="panel-title">控制</h3>
            <div className="panel-row">
              <button className="btn btn-primary" onClick={powerOn} disabled={state.status === 'idle'}>
                开机
              </button>
              <button className="btn" onClick={powerOff} disabled={state.status === 'idle'}>
                关机
              </button>
            </div>
            <div className="panel-row">
              <button className="btn" onClick={reset} disabled={!running}>
                复位
              </button>
              <button
                className="btn"
                onClick={state.status === 'paused' ? resume : pause}
                disabled={state.status !== 'running' && state.status !== 'paused'}
              >
                {state.status === 'paused' ? '继续' : '暂停'}
              </button>
            </div>
          </div>

          <div className="panel">
            <h3 className="panel-title">机型</h3>
            <div className="machine-select-row">
              <select
                className="machine-select"
                value={selectedMachine}
                onChange={(e) => handleMachinePick(e.target.value)}
                disabled={bootingId !== null}
                aria-label="选择机型"
              >
                <optgroup label="经典 FC">
                  <option value={OPTION_FC}>经典 FC 卡带</option>
                </optgroup>
                {machineGroups.map((g) => (
                  <optgroup key={g} label={g}>
                    {machines
                      .filter((m) => m.group === g)
                      .map((m) => (
                        <option key={m.file} value={m.file} title={m.desc}>
                          {m.label}
                        </option>
                      ))}
                  </optgroup>
                ))}
                <optgroup label="其它">
                  <option value={OPTION_BROWSE}>浏览本地 ROM…</option>
                </optgroup>
              </select>
              {bootingId !== null && <span className="spin" aria-hidden="true" />}
            </div>
            {/* 选择本地文件用的隐藏 input —— 选中「浏览本地 ROM…」时由代码点开 */}
            <input
              ref={romFileRef}
              className="hidden-input"
              type="file"
              accept=".nes,.NES"
              onChange={(e) => {
                const f = e.target.files?.[0];
                if (f) handleLoadRom(f);
                e.target.value = '';
                // 选完文件把下拉退回到原选项，避免它一直停在「浏览本地 ROM…」
                setSelectedMachine(prev => (prev === OPTION_BROWSE ? '' : prev));
              }}
            />
            <p className="builtin-hint">
              {bootingId !== null ? '正在加载并开机…' : '选中即加载并开机'}
            </p>
            {state.romName && <div className="rom-name" title={state.romName}>{state.romName}</div>}
          </div>

          <div className="panel">
            <h3 className="panel-title">音量</h3>
            <div className="panel-row">
              <input
                type="range"
                min={0}
                max={1}
                step={0.05}
                value={volume}
                onChange={(e) => handleVolumeChange(Number(e.target.value))}
              />
              <span className="vol-val">{Math.round(volume * 100)}%</span>
            </div>
          </div>

          {hasAnyPeripheral && (
            <PeripheralPanel
              floppyDriveCount={state.floppyDriveCount}
              floppyStates={floppyStates}
              floppyMotorRef={floppyMotorRef}
              onFloppyInsert={floppyInsert}
              onFloppyInsertBlank={floppyInsertBlank}
              onFloppyEject={floppyEject}
              onFloppySave={floppySaveImage}
              onFloppyWriteProtect={floppySetWriteProtected}
              hasPrinter={state.hasPrinter}
              printerPages={printerPages}
              onGetPrinterPage={getPrinterLastPage}
              hasTape={state.hasTape}
              tapeLoaded={tapeLoaded}
              tapePlaying={tapePlaying}
              onTapeLoad={tapeLoad}
              onTapeRewind={tapeRewind}
            />
          )}

          <div className="panel">
            <label className="switch">
              <input
                type="checkbox"
                checked={showTouchControls}
                onChange={(e) => {
                  setShowTouchControls(e.target.checked);
                  /* 打开手柄时顺手收起侧栏面板 —— 窄屏上侧栏是抽屉，
                     它展开时正好盖住底部的触屏手柄，用户会以为手柄
                     没出来（实测：390px 视口下手柄在 y=699，面板覆盖
                     同区域）。收起后手柄直接可见、也能立刻按。
                     关闭手柄时不主动改面板，交给用户自己控制。 */
                  if (e.target.checked) setDrawerOpen(false);
                }}
              />
              触屏手柄
            </label>

            {/* 虚拟键盘开关只在学习机机型出现 —— FC 卡带没有键盘，
                显示出来只会让人困惑。点开即弹系统输入法。 */}
            {state.isLearningMachine && (
              <label className="switch">
                <input
                  type="checkbox"
                  checked={showVirtualKeyboard}
                  onChange={(e) => {
                    setShowVirtualKeyboard(e.target.checked);
                    /* 和手柄一致：打开时收起面板，否则键盘被抽屉挡住 */
                    if (e.target.checked) setDrawerOpen(false);
                  }}
                />
                虚拟键盘
              </label>
            )}
          </div>
          </div>
        </aside>
        {/* 把手固定贴在容器底部，面板是绝对定位、叠在它上方单独滑动。
            把手的 DOM 位置和视觉位置都不参与面板的动画，所以永远不会瞬移，
            也不需要任何同步代码。
            用真实按钮而不是伪元素 —— 伪元素收不到点击，手指按上去会被当成画面触摸。 */}
        <button
          className="drawer-handle"
          onClick={() => setDrawerOpen((v) => !v)}
          aria-label={drawerOpen ? '收起面板' : '展开面板'}
          aria-expanded={drawerOpen}
          type="button"
        >
          <span className="drawer-handle-bar" />
        </button>
        </div>
      </main>

      {/* ---- 底部栏栈 ----
          触屏手柄、虚拟键盘栏、底栏全部放进同一个文档流容器。
          这样它们自然上下堆叠、互不重叠，而 main 是 flex:1，
          画面区会自动收缩到剩下的高度，永远不会被顶出屏幕。

          为什么不再用 fixed 定位：fixed 元素脱离文档流、不占高度，
          main 不知道它存在，于是画面按全屏高度排版，被底部栏盖住/顶出。
          之前靠 calc 手算让位，只要多一条栏就必然算错。 */}
      <div className="bottom-stack" ref={stackRef}>
        {showTouchControls && (
          <div className="touch-bar">
            <OnScreenController port={1} onButtonDown={pressButton} onButtonUp={releaseButton} />
          </div>
        )}


        <footer className="bottombar">
          {state.isLearningMachine ? (
            <span>学习机键盘直通 · 方向键/Z/X/Enter 同时映射手柄</span>
          ) : (
            <span>方向键 移动 · Z = A · X = B · Shift = Select · Enter = Start</span>
          )}
          {state.hasMouse && <span className="hint-mouse">鼠标直接在画面上操作</span>}
        </footer>
      </div>

      <KeyboardController
        port={1}
        onButtonDown={pressButton}
        onButtonUp={releaseButton}
        enabled={running}
        learningMachineMode={state.isLearningMachine}
        onSendKey={sendKey}
      />
      <KeyboardController
        port={2}
        onButtonDown={pressButton}
        onButtonUp={releaseButton}
        enabled={running}
      />

      {/* 手机虚拟键盘：隐藏 textarea 锚住系统输入法，
          打出来的字符翻译成 KeyboardEvent.code 送进模拟器。 */}
      <VirtualKeyboard
        ref={vkbRef}
        enabled={showVirtualKeyboard && running}
        onSendKey={sendKey}
        hasKeyboard={state.isLearningMachine}
      />

      <AboutDialog
        open={showAbout}
        onClose={() => setShowAbout(false)}
        version={RELEASE_VERSION}
      />
    </div>
  );
}
