import { forwardRef, useCallback, useEffect, useImperativeHandle, useRef } from 'react';

interface VirtualKeyboardProps {
  /** 键盘是否启用（由侧栏开关控制） */
  enabled: boolean;
  /** 发给模拟器的按键回调，参数为 KeyboardEvent.code */
  onSendKey: (code: string, down: boolean) => boolean;
  /** 当前机型是否有内置键盘；没有就不渲染锚点 */
  hasKeyboard: boolean;
}

/** 供外层按键条调用的方法 */
export interface VirtualKeyboardHandle {
  /** 聚焦隐藏输入框，唤起系统输入法 */
  focus: () => void;
}

/**
 * 手机端虚拟键盘的**锚点**部分 —— 只有一个透明的 textarea。
 *
 * 界面上的按键条由 App 渲染在 .bottom-stack 里，和触屏手柄、底栏一起
 * 走文档流（这样几条栏自然堆叠、不会互相压住，画面区也能自动收缩）。
 * 本组件只负责三件事：
 *   1) 藏一个可输入元素锚住系统输入法
 *   2) 把输入法打出来的字符翻译成 KeyboardEvent.code 送进模拟器
 *   3) 通过 ref 暴露 focus() 给按键条上的「调出键盘」按钮
 *
 * 为什么要锚点：手机浏览器只有在**可输入元素真正获得焦点**时才弹系统键盘，
 * 模拟器画布不是可输入元素，学习机启动后随手要打字时根本唤不出键盘。
 *
 * 关键取舍：
 * 1) **不用 keydown 收字符**。手机输入法（尤其中文）走 composition，
 *    keydown 的 key 是 'Unidentified'、code 是 ''，拿不到实际字符。
 * 2) **每次只处理「新增」的字符**，处理完立刻清空 textarea。
 * 3) **软键盘上没有「按键松开」**，每个字符发一次 down + 一次 up。
 * 4) **Enter / Backspace 靠 beforeinput**，系统输入法发这两个键时
 *    不派发 keydown，只有 beforeinput。
 */
export const VirtualKeyboard = forwardRef<VirtualKeyboardHandle, VirtualKeyboardProps>(
  function VirtualKeyboard({ enabled, onSendKey, hasKeyboard }, ref) {
    const inputRef = useRef<HTMLTextAreaElement | null>(null);
    /* 上一次处理完的文本内容。input 事件里拿到的 value 是「累加」的，
       用它做差得到本次新增的那部分。 */
    const lastValueRef = useRef('');

    /* 按键队列：串行发送，保证「松开上一个」一定发生在「按下下一个」之前。
       不加队列时连续输入 "ab" 会变成 down(a) down(b) up(a) up(b)，
       核心看到的是一团乱序状态，回显可能只出一个字甚至不出。 */
    const queueRef = useRef<Promise<void>>(Promise.resolve());

    /* 把页面拉回原点。
       软键盘弹出时浏览器会为了露出聚焦元素滚动整个页面，表现就是
       模拟器画面被顶到屏幕外面。**只在校验一次不够**：键盘展开有动画，
       浏览器往往在动画结束后再滚一次，所以连续校验几拍。 */
    const resetScroll = useCallback(() => {
      const fix = () => {
        window.scrollTo(0, 0);
        document.documentElement.scrollTop = 0;
        document.body.scrollTop = 0;
      };
      [0, 50, 150, 300, 500, 800].forEach((ms) => setTimeout(fix, ms));
    }, []);

    /* 供外层「调出键盘」按钮调用 */
    useImperativeHandle(
      ref,
      () => ({
        focus: () => {
          const el = inputRef.current;
          if (!el) return;
          /* 保留占位符，方向键检测需要有可移动的光标 */
          el.value = '\u200b'.repeat(20);
          el.setSelectionRange(10, 10);
          lastValueRef.current = '';
          try {
            /* preventScroll 是关键：不让浏览器为了露出元素而滚动页面 */
            el.focus({ preventScroll: true });
          } catch {
            el.focus();
          }
          resetScroll();
        },
      }),
      [resetScroll],
    );

    /* 把一个可打印字符翻译成 KeyboardEvent.code。
       KeyboardEvent.code 是「物理键位」而不是字符，所以要反查：
       字母 → KeyA..KeyZ���数字 → Digit0..Digit9；常见符号单独映射。

       上档符号（! @ # $ % ^ & * ( ) _ + { } | : " < > ? ~）在真键盘上
       是 Shift + 基础键**两个键的组合**。只发基础键等于输入了它的下档字符
       （如 ! 变成 1、+ 变成 =），这正是「有些符号输入不对」的原因。
       所以返回 [code, 需要Shift] 二元组。 */
    const charToKey = useCallback((ch: string): [string, boolean] | null => {
      if (/^[a-zA-Z]$/.test(ch)) return ['Key' + ch.toUpperCase(), false];
      if (/^[0-9]$/.test(ch)) return ['Digit' + ch, false];
      if (ch === ' ') return ['Space', false];

      /* 下档符号：直接对应一个物理键 */
      const lower: Record<string, string> = {
        ',': 'Comma',
        '.': 'Period',
        '/': 'Slash',
        ';': 'Semicolon',
        "'": 'Quote',
        '[': 'BracketLeft',
        ']': 'BracketRight',
        '\\': 'Backslash',
        '-': 'Minus',
        '=': 'Equal',
        '`': 'Backquote',
      };
      if (lower[ch]) return [lower[ch], false];

      /* 上档符号：Shift + 基础键 */
      const upper: Record<string, string> = {
        ':': 'Semicolon',
        '"': 'Quote',
        '{': 'BracketLeft',
        '}': 'BracketRight',
        '|': 'Backslash',
        '_': 'Minus',
        '+': 'Equal',
        '~': 'Backquote',
        '<': 'Comma',
        '>': 'Period',
        '?': 'Slash',
        '!': 'Digit1',
        '@': 'Digit2',
        '#': 'Digit3',
        $: 'Digit4',
        '%': 'Digit5',
        '^': 'Digit6',
        '&': 'Digit7',
        '*': 'Digit8',
        '(': 'Digit9',
        ')': 'Digit0',
      };
      if (upper[ch]) return [upper[ch], true];

      return null;
    }, []);

    /* 发一次完整敲击：按下 + 保持 120ms + 松开。
       为什么要保持：裕兴的 keyboard-yuxing.cpp 里 setKey() 只改 down_
       标志位、重建行数据，**不产生中断** —— 主程序要自己轮询行选寄存器
       才发现按键。保持时间太短时程序还没扫到那一行就已经松开了，等于没按
       （实测 40ms 不够）。120ms ≈ 6 个 50Hz 帧，够主循环扫完一轮键表。 */
    const tapCode = useCallback(
      (code: string, needShift = false) => {
        queueRef.current = queueRef.current.then(
          () =>
            new Promise<void>((resolve) => {
              /* 上档符号要先按住 Shift，再按主键 —— 顺序反了核心会当成
                 普通键处理，输出的是下档字符。松开顺序也要反过来：
                 先松主键再松 Shift，否则后续字符会被误判成上档。 */
              if (needShift) onSendKey('ShiftLeft', true);
              onSendKey(code, true);
              setTimeout(() => {
                onSendKey(code, false);
                if (needShift) onSendKey('ShiftLeft', false);
                resolve();
              }, 120);
            }),
        );
      },
      [onSendKey],
    );

    /* ---- 输入法文字：从 input 事件里取新增字符 ----
       textarea 里有方向键检测用的占位零宽字符（\u200b），用户输入的字符
       可能插在占位符中间。所以**不能按长度切**（剥掉占位符后长度对不上），
       而是逐字比对：把占位符去掉，跟上次的内容做差分，取新增的那部分。 */
    const handleInput = useCallback(() => {
      const el = inputRef.current;
      if (!el) return;

      /* 去掉占位零宽字符 —— 剩下的就是用户输入的字符 */
      const typed = el.value.replace(/\u200b/g, '');

      /* 只处理本次新增。输入法在 composition 期间会反复触发 input，
         每次都带完整的候选串，所以取「比上次多出来的那部分」。 */
      if (typed.length > lastValueRef.current.length) {
        const added = typed.slice(lastValueRef.current.length);
        for (const ch of added) {
          const mapped = charToKey(ch);
          if (mapped) tapCode(mapped[0], mapped[1]);
        }
      }
      lastValueRef.current = typed;

      /* 清掉用户输入，保留占位符供方向键检测，光标回到中点 */
      el.value = '\u200b'.repeat(20);
      el.setSelectionRange(10, 10);
      lastValueRef.current = '';
    }, [charToKey, tapCode]);

    /* ---- 输入法专用的 beforeinput ----
       手机上从系统输入法按「回车」和「退格」，**不会派发 keydown**
       （实测：只发 beforeinput，inputType 分别是 insertLineBreak 和
       deleteContentBackward，code 为空）。只监听 keydown 的写法在手机上
       必然丢这两个键 —— 这正是「回车和回退键没反应」的原因。

       注意：**不能用 React 的 onBeforeInput**。React 在部分浏览器/输入法
       组合下不合成这个事件（实测挂 onBeforeInput 后画面零变化），
       必须手动 addEventListener 绑原生 beforeinput。 */
    useEffect(() => {
      const el = inputRef.current;
      if (!el) return;
      const onBeforeInput = (ev: Event) => {
        const native = ev as InputEvent;
        const type = native.inputType;
        if (type === 'insertLineBreak' || type === 'insertParagraph') {
          ev.preventDefault();
          tapCode('Enter');
          return;
        }
        if (type === 'deleteContentBackward') {
          ev.preventDefault();
          tapCode('Backspace');
          return;
        }
        if (type === 'deleteContentForward') {
          ev.preventDefault();
          tapCode('Delete');
        }
      };
      el.addEventListener('beforeinput', onBeforeInput);
      return () => el.removeEventListener('beforeinput', onBeforeInput);
      /* 依赖里必须有 hasKeyboard：机型从 FC 切到学习机时组件先返回 null
         （textarea 还不存在，ref 为 null），随后才真正渲染出 textarea。
         只依赖 tapCode 的话这个 effect 不会再跑，监听就永远绑不上 ——
         实测表现为「事件能到 textarea，但 defaultPrevented 一直是 false」。 */
    }, [tapCode, hasKeyboard, enabled]);

    /* ---- 诊断：把收到的所有键盘/输入事件记录下来 ----
       手机上输入法的差异极大，光看文档猜不出来。打开 ?kbd=1 后在输入法里
       按一下出问题的键，页面上会列出浏览器实际发出的 code/key/keyCode，
       据此判断是解析漏了还是信息本身丢失。 */
    useEffect(() => {
      if (typeof window === 'undefined') return;
      if (!new URLSearchParams(window.location.search).has('kbd')) return;
      const log: string[] = [];
      const show = () => {
        let box = document.getElementById('__kbd_log');
        if (!box) {
          box = document.createElement('pre');
          box.id = '__kbd_log';
          box.style.cssText =
            'position:fixed;left:0;right:0;top:40px;z-index:99999;max-height:45vh;overflow:auto;' +
            'margin:0;padding:8px;font-size:11px;line-height:1.5;color:#c9d1d9;' +
            'background:rgba(13,17,23,.94);border-bottom:1px solid #30363d;white-space:pre-wrap';
          document.body.appendChild(box);
        }
        box.textContent = log.slice(-14).join('\n');
      };
      const rec = (tag: string, e: Event) => {
        const k = e as KeyboardEvent;
        const i = e as InputEvent;
        log.push(
          `${tag} code="${k.code ?? ''}" key="${k.key ?? ''}" keyCode=${k.keyCode ?? '-'} ` +
            `inputType="${i.inputType ?? ''}" data="${(i.data ?? '').toString().slice(0, 8)}"`,
        );
        show();
      };
      const el = inputRef.current;
      const onKD = (e: Event) => rec('keydown', e);
      const onBI = (e: Event) => rec('beforeinput', e);
      const onIN = (e: Event) => rec('input', e);
      el?.addEventListener('keydown', onKD, true);
      el?.addEventListener('beforeinput', onBI, true);
      el?.addEventListener('input', onIN, true);
      window.addEventListener('keydown', onKD, true);
      return () => {
        el?.removeEventListener('keydown', onKD, true);
        el?.removeEventListener('beforeinput', onBI, true);
        el?.removeEventListener('input', onIN, true);
        window.removeEventListener('keydown', onKD, true);
        document.getElementById('__kbd_log')?.remove();
      };
    }, [enabled, hasKeyboard]);

    /* ---- 特殊键：Enter / Backspace / Tab / 方向键 ----
       手机输入法的方向键**不一定带 code**。实测三种形态：
         A 正常键盘：code="ArrowLeft", key="ArrowLeft", keyCode=37
         B 部分输入法：code="", key="Unidentified", keyCode=229
         C 另一些输入法：code="", key="ArrowLeft", keyCode=37
       只看 e.code 会漏掉 B 和 C —— 这就是「方向键不能用」的原因。
       所以依次用 code → key → keyCode 兜底解析。 */
    const handleKeyDown = useCallback(
      (e: React.KeyboardEvent<HTMLTextAreaElement>) => {
        const special = [
          'Enter',
          'Backspace',
          'Tab',
          'Escape',
          'ArrowUp',
          'ArrowDown',
          'ArrowLeft',
          'ArrowRight',
          'Home',
          'End',
          'Delete',
        ];
        /* keyCode → code 对照（W3C 旧标准值，移动端输入法普遍仍在使用） */
        const byKeyCode: Record<number, string> = {
          8: 'Backspace',
          9: 'Tab',
          13: 'Enter',
          27: 'Escape',
          35: 'End',
          36: 'Home',
          37: 'ArrowLeft',
          38: 'ArrowUp',
          39: 'ArrowRight',
          40: 'ArrowDown',
          46: 'Delete',
        };

        let code = e.code;
        if (!code || !special.includes(code)) {
          /* 第二选择：e.key。注意方向键和功能键的 key 与 code 同名 */
          const byKey = e.key;
          if (byKey && special.includes(byKey)) {
            code = byKey;
          } else if (e.keyCode && byKeyCode[e.keyCode]) {
            /* 第三选择：keyCode。B 形态里 key 是 'Unidentified'，
               只有 keyCode 还能用。 */
            code = byKeyCode[e.keyCode];
          }
        }

        /* keyCode 229 是 Android 输入法「正在合成」的专用值。
           注意：**不能见到 229 就直接 return**。手机输入法发左右方向键时，
           常见 code=""、key="Unidentified"、keyCode=229 的组合，若先判 229
           就把后面能救回来的逻辑全跳过了 —— 实测表现正是「上下键能用、
           左右键没反应」（上下键的 keyCode 通常是正常的 38/40）。
           所以先解析，解析不出具体键才放弃。 */
        if (!special.includes(code)) return;
        /* 阻止默认：Backspace 在空 textarea 里会触发浏览器后退，
           Enter 可能提交表单，方向键会移动光标。 */
        e.preventDefault();
        tapCode(code);
      },
      [tapCode],
    );

    /* ---- 兜底通路：光标移动 ----
       不少 Android 输入法的左右方向键**不在 keydown 里**，而是直接让
       输入光标移动（浏览器内部处理，只留下 selectionchange）。
       上下键在 textarea 里没有「光标上下移动」这个动作，所以只有左右键
       会走到这条路 —— 正好对应「上下能用、左右没反应」的表现。

       做法：给 textarea 塞一段占位文本，让光标有地方可移（内容为空时
       左右键移不动光标，检测不到）。每次 selectionchange 看光标是否偏离
       中点：往右移说明按了右键，往左移说明按了左键，发一次对应按键，
       然后把光标复位到中点，这样连按同一个键也能持续检测到。

       占位文本不能参与输入解析：handleInput 只处理新增字符，而这里
       插入的内容不会触发 input 事件（直接改 value 不走 input）。 */
    useEffect(() => {
      if (!enabled) return;
      const el = inputRef.current;
      if (!el) return;
      const FILLER = '\u200b'.repeat(20);
      const MID = 10;
      const arm = () => {
        el.value = FILLER;
        el.setSelectionRange(MID, MID);
      };
      arm();
      const onSel = () => {
        if (document.activeElement !== el) return;
        const pos = el.selectionStart ?? MID;
        if (pos === MID) return;
        const right = pos > MID;
        onSendKey(right ? 'ArrowRight' : 'ArrowLeft', true);
        setTimeout(() => onSendKey(right ? 'ArrowRight' : 'ArrowLeft', false), 120);
        /* 复位到中点，下一次按键仍能判别方向 */
        el.setSelectionRange(MID, MID);
      };
      document.addEventListener('selectionchange', onSel);
      return () => {
        document.removeEventListener('selectionchange', onSel);
        el.value = '';
      };
    }, [enabled, hasKeyboard, onSendKey]);

    /* 打开开关就自动聚焦 → 系统键盘直接弹出来，不用再点一次。
       focus 必须发生在用户手势里才允许弹键盘，这里由开关的 onChange
       触发渲染，紧接着的 effect 仍算在手势链上；个别浏览器（iOS Safari）
       会拒绝，所以按键条上保留「调出键盘」按钮手动点一次。 */
    useEffect(() => {
      if (!enabled) return;
      const el = inputRef.current;
      if (!el) return;
      el.value = '\u200b'.repeat(20);
      el.setSelectionRange(10, 10);
      lastValueRef.current = '';
      try {
        el.focus({ preventScroll: true });
      } catch {
        el.focus();
      }
      resetScroll();

      /* 键盘打开的整个期间持续看守：任何时刻检测到页面被滚动就立刻归零。
         真机上浏览器会为了「露出聚焦元素」自行滚动，时机不固定
         （输入法切换、键盘高度变化都可能触发），只靠聚焦那一下修不住。 */
      const guard = () => {
        if (window.scrollY !== 0 || document.documentElement.scrollTop !== 0) {
          window.scrollTo(0, 0);
          document.documentElement.scrollTop = 0;
          document.body.scrollTop = 0;
        }
      };
      const timer = window.setInterval(guard, 200);
      window.addEventListener('scroll', guard, true);

      return () => {
        window.clearInterval(timer);
        window.removeEventListener('scroll', guard, true);
      };
    }, [enabled, resetScroll]);

    if (!hasKeyboard) return null;

    return (
      /* 锚点输入框。
         不能 display:none 或 visibility:hidden —— 那样拿不到焦点、
         系统键盘不会弹。所以用「1px 大小 + 透明 + 定位在视口内」藏起来。 */
      <textarea
        ref={inputRef}
        aria-label="学习机键盘输入"
        autoCapitalize="off"
        autoCorrect="off"
        autoComplete="off"
        spellCheck={false}
        onInput={handleInput}
        onKeyDown={handleKeyDown}
        style={{
          position: 'fixed',
          left: 0,
          /* **贴在视口顶部，不要贴底部**。
             软键盘从屏幕底部弹出，浏览器为了让聚焦元素可见，会把页面
             往上滚 —— 元素在底部时这一滚正好把顶栏和画面上半部分推出
             屏幕（用户看到的就是「画面被顶出去」）。
             放在顶部：软键盘弹出后它仍在可见区内（软键盘只吃底部），
             浏览器没有任何理由滚动页面，顶栏和画面就都留在原位。 */
          top: 0,
          /* 显式归零：CSS 里 body 的 inset 之类会影响，留着 auto 会算错 */
          bottom: 'auto',
          right: 'auto',
          width: 1,
          height: 1,
          opacity: 0,
          border: 'none',
          padding: 0,
          margin: 0,
          resize: 'none',
          /* iOS 上 font-size < 16px 会在聚焦时自动放大页面，设 16 规避 */
          fontSize: 16,
          zIndex: -1,
          pointerEvents: 'none',
        }}
      />
    );
  },
);
