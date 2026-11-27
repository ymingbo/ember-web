import { useCallback, useRef } from 'react';
import type { PadButton } from '../types/ember';

interface OnScreenControllerProps {
  port: 1 | 2;
  onButtonDown: (port: 1 | 2, button: PadButton) => void;
  onButtonUp: (port: 1 | 2, button: PadButton) => void;
}

/**
 * 触屏/虚拟手柄 —— 红白机（FC）外观。
 *
 * 造型照 FC 手柄：米白机身、左侧黑色十字键、右侧两颗圆键
 * （B 在左、A 在右，与真机同侧序）、中间 SELECT / START 两颗胶囊键，
 * 机身上沿一道红色饰条。
 *
 * 实现要点：
 *   - 十字键是**一个连体十字**（不是四个圆按钮）：用两块矩形叠成一个
 *     十字底板，再在其上按方向铺四条可点击的臂。真机的十字是一个
 *     整体、按哪边就是哪个方向，四个离散圆钮看着就是个方向板而不是手柄。
 *   - 所有可点区域都 setPointerCapture，手指按下去滑出边界也不会漏 UP
 *     （漏 UP 的后果是那个方向一直按着不放，人物自己走）。
 *   - touch-none + preventDefault：避免浏览器把手势当成滚动/双击缩放。
 *   - 尺寸用 clamp 跟屏宽走：窄屏能塞下，宽屏不会长得很大。
 *   - 纯 CSS 绘制，不用图片：任何 DPI 都清晰，也不用多带一份资源。
 */

/** 十字键每个**方形**键的边长（px）。 */
const SQ = 24;
/** 十字键四键之间的缝隙（px）。 */
const DQ_GAP = 2;
/** A/B 圆键直径（px）。 */
const ROUND_KEY = 36;

export function OnScreenController({
  port,
  onButtonDown,
  onButtonUp,
}: OnScreenControllerProps) {
  /* 记录每个按钮当前是否已经按下 —— 防止 pointerdown 之后连续收到
     pointerleave/pointercancel 重复发 UP，也防止重复发 DOWN 让机器侧
     的按键状态错乱。 */
  const heldRef = useRef<Set<PadButton>>(new Set());

  const press = useCallback(
    (button: PadButton) => (e: React.PointerEvent) => {
      e.preventDefault();
      e.stopPropagation();
      if (heldRef.current.has(button)) return;
      heldRef.current.add(button);
      /* 捕获指针：手指滑出手柄范围后仍能收到 pointerup */
      try {
        (e.currentTarget as HTMLElement).setPointerCapture(e.pointerId);
      } catch {
        /* 某些浏览器在 pointercancel 后再捕获会抛，忽略即可，
           反正下面的 pointerup 兜底也会发 UP。 */
      }
      onButtonDown(port, button);
    },
    [port, onButtonDown],
  );

  const release = useCallback(
    (button: PadButton) => (e: React.PointerEvent) => {
      e.preventDefault();
      e.stopPropagation();
      if (!heldRef.current.has(button)) return;
      heldRef.current.delete(button);
      onButtonUp(port, button);
    },
    [port, onButtonUp],
  );

  /** 单个可点区域共用的 handler 集合。 */
  const bind = (button: PadButton) => ({
    onPointerDown: press(button),
    onPointerUp: release(button),
    onPointerCancel: release(button),
  });

  /* 十字键单个**方形**键。四键同款：方底、圆角、按下变亮并轻微下沉，
     和真机十字键单键的手感一致。 */
  const sqKey =
    'touch-none select-none cursor-pointer flex items-center justify-center ' +
    'active:brightness-150 transition-[filter] duration-75';

  /** 方形键底色 —— 比机身深得多的近黑，和米白外壳形成强对比。 */
  const sqKeyStyle: React.CSSProperties = {
    background: 'linear-gradient(180deg,#2a2a2a,#161616 60%,#0d0d0d)',
    border: '1px solid #000',
    borderRadius: 4,
    boxShadow:
      'inset 0 1px 0 rgba(255,255,255,.16), 0 1px 2px rgba(0,0,0,.5)',
  };

  /* 十字键不画方向箭头 —— 用户要求「箭头老是画不对，索性不画，
     就保持黑色方块」。四个方形键本身就是经典的十字键外观，
     方向由位置表达，比在 24px 的方键里塞一个 8px 三角更清楚。
     （曾用 CSS 边框三角画过，朝向规则是「尖朝有颜色边的对面」，
      反复调仍然观感不对，用户明确放弃箭头。） */

  return (
    <div className="pad-wrap">
      {/* ---- 机身 ---- */}
      <div
        className="relative flex items-center select-none"
        style={{
          /* 关键：机身必须 width:fit-content。
             父层是 flex + justify-center + w-full，机身作为 flex item
             会被拉成整行宽；只写 flex:'0 0 auto' 挡不住（那是主轴方向，
             ���里拉伸来自 align-items 的 stretch）。撑开后十字键与 A/B
             之间隔着一大片空白，看着就是"手柄被横向拉扁"。 */
          /* 尺寸显式写死，不靠内容推导：
               fit-content 在 flex item 上算不准（实测 112px，内容该 250+）；
               完全不写又会被父层拉伸到整行宽。
             宽 = 十字 76 + 14 + SEL/START 98 + 14 + A/B 88 + padding 28
             高 = 108：A/B 圆键要贴到十字键下键那条水平线上，
                   36px 的圆键比 76px 的十字键更靠下，机身必须留够高度 */
          display: 'flex',
          flexDirection: 'row',
          alignItems: 'center',
          /* 用 flexGrow/Shrink/Basis 长写，**不用 flex 简写** —— 简写会和
             Tailwind `flex` 类设的 display:flex 冲突，实测症状是三组
             元素纵向堆在左侧、机身里全空。 */
          flexGrow: 0,
          flexShrink: 0,
          flexBasis: 'auto',
          width: 318,
          height: 108,
          boxSizing: 'border-box',
          gap: 14,
          padding: '8px 14px 10px',
          borderRadius: 14,
          /* FC 机身：上浅下深的暖白，加一圈淡金边模拟塑料壳 */
          background:
            'linear-gradient(180deg,#e8e0d0 0%,#d9cfb9 55%,#c8bda4 100%)',
          border: '1px solid #a89b80',
          boxShadow:
            '0 3px 10px rgba(0,0,0,.45), inset 0 1px 0 rgba(255,255,255,.65)',
        }}
      >
        {/* ---- 十字键：四个**方形**键按十字排列 ---- */}
        {/* 用户指定要方形键，不要连体十字底板。
            布局：3x3 网格只占四边中格 + 中心一格留空，
            每键 SQ 见方，键与键之间留 GAP 的缝，看着就是经典十字键。 */}
        <div
          className="relative shrink-0"
          style={{
            /* display 必须显式写：只写 gridTemplate* 时容器仍是 block，
               子元素按普通流排下来（实测四个键全挤在上两行、尺寸被
               内容撑成 26x19）。Tailwind 的 `grid` 类在这里没生效。 */
            display: 'grid',
            gridTemplateColumns: `repeat(3, ${SQ}px)`,
            gridTemplateRows: `repeat(3, ${SQ}px)`,
            gap: DQ_GAP,
            width: SQ * 3 + DQ_GAP * 2,
            height: SQ * 3 + DQ_GAP * 2,
          }}
        >
          {/* 上 */}
          <button
            {...bind('UP')}
            aria-label="上"
            className={sqKey}
            style={{ ...sqKeyStyle, gridColumn: 2, gridRow: 1 }}
          >
          </button>
          {/* 左 */}
          <button
            {...bind('LEFT')}
            aria-label="左"
            className={sqKey}
            style={{ ...sqKeyStyle, gridColumn: 1, gridRow: 2 }}
          >
          </button>
          {/* 右 */}
          <button
            {...bind('RIGHT')}
            aria-label="右"
            className={sqKey}
            style={{ ...sqKeyStyle, gridColumn: 3, gridRow: 2 }}
          >
          </button>
          {/* 下 */}
          <button
            {...bind('DOWN')}
            aria-label="下"
            className={sqKey}
            style={{ ...sqKeyStyle, gridColumn: 2, gridRow: 3 }}
          >
          </button>
        </div>

        {/* ---- SELECT / START：**水平并排**两颗短胶囊键 ---- */}
        {/* 用户指定：中间两颗键要横着排，不要竖排；每颗键短一点。
            容器必须定宽：不写宽度时 flex 会把它拉到剩余全部空间，
            机身跟着变宽到 760px，十字键和 A/B 之间隔着一大片空白。 */}
        <div
          className="shrink-0"
          /* 两键间距：用户要求「稍微分开一点点」。
             6px 时两颗 42px 的胶囊几乎连成一条，14px 才有明确分界。
             display 必须显式写 —— Tailwind 的 flex-row/items-center 类
             在本项目没生成（实测 gap 写了但间距仍是 0，说明容器根本不是
             flex，两个键按块级流排下来的）。 */
          style={{
            display: 'flex',
            flexDirection: 'row',
            alignItems: 'center',
            gap: 14,
          }}
        >
          {(
            [
              ['SELECT', 'SELECT'],
              ['START', 'START'],
            ] as const
          ).map(([btn, label]) => (
            <button
              key={btn}
              {...bind(btn as PadButton)}
              aria-label={label}
              className="touch-none select-none cursor-pointer active:translate-y-[1px]"
              style={{
                width: 42,
                height: 14,
                borderRadius: 7,
                fontSize: 6.5,
                letterSpacing: 0.1,
                fontWeight: 700,
                color: '#f0e6d2',
                /* 真机 SELECT/START 是深棕红橡胶键，比机身明显深一档，
                   之前用深灰会和机身糊在一起。 */
                background:
                  'linear-gradient(180deg,#8a5a3a,#5d3320 55%,#3d1f11)',
                border: '1px solid #2b1408',
                boxShadow:
                  'inset 0 1px 0 rgba(255,255,255,.28), 0 1px 2px rgba(0,0,0,.45)',
                /* 真机 SELECT/START 是水平放置的胶囊键，不做旋转。
                   之前照美版 NES 手柄的斜置做了 -25°，在 6px 字号下
                   标签跟着歪，反而看不清。 */
                textShadow: '0 1px 1px rgba(0,0,0,.5)',
              }}
            >
              {label}
            </button>
          ))}
        </div>

        {/* ---- A / B 圆键：**B 在左、A 在右**，两键拉开间距 ----
            用户要求：
              1. 圆形（之前 borderRadius 是 0px —— rounded-full 这个
                 Tailwind 类在本项目没生效，必须内联写 borderRadius）；
              2. B 在左、A 在右（和真机同序；中途曾按「A 在左」改过一版，
                 用户看过实物后要求换回来）；
              3. 两键中间要分开，不能挤在一起；
              4. 与十字键的**下键**在同一水平线上。

            对齐做法：A/B 容器用 alignSelf:'flex-end' 贴机身底部，
            容器高度 = 圆键直径，这样圆心自然落到底边中线；
            十字键在机身里垂直居中，它的下键底边也贴着同一侧，
            两边圆心落在同一条水平线上。 */}
        <div
          className="shrink-0"
          style={{
            display: 'flex',
            flexDirection: 'row',
            alignItems: 'center',
            /* 两键间距：直径 36 的两颗圆键，留 16px 空档，
              视觉上明显分开又不至于隔成两坨。 */
            gap: 16,
            /* 贴底部对齐，让圆心与十字键下键同高。
               marginBottom 实测调 0 时圆心差 6px → 0，两边落在同一条
               水平线上；之前给 6px 时 A/B 比下键高 6px。 */
            alignSelf: 'flex-end',
            marginBottom: 0,
          }}
        >
          {/* B 键：左 */}
          <button
            {...bind('B')}
            aria-label="B"
            className="touch-none select-none cursor-pointer flex items-center justify-center font-bold"
            style={{
              width: ROUND_KEY,
              height: ROUND_KEY,
              borderRadius: ROUND_KEY / 2,
              fontSize: 13,
              color: '#f3ead8',
              background:
                'radial-gradient(circle at 34% 26%,#8e3030,#a83232 46%,#5e1919)',
              border: '1px solid #2e0d0d',
              boxShadow:
                'inset 0 2px 3px rgba(255,255,255,.1), inset 0 -2px 4px rgba(0,0,0,.35), 0 3px 6px rgba(0,0,0,.45)',
              textShadow: '0 1px 1px rgba(0,0,0,.45)',
            }}
          >
            B
          </button>

          {/* A 键：右 */}
          <button
            {...bind('A')}
            aria-label="A"
            className="touch-none select-none cursor-pointer flex items-center justify-center font-bold"
            style={{
              width: ROUND_KEY,
              height: ROUND_KEY,
              borderRadius: ROUND_KEY / 2,
              fontSize: 13,
              color: '#f3ead8',
              background:
                'radial-gradient(circle at 34% 26%,#8e3030,#a83232 46%,#5e1919)',
              border: '1px solid #2e0d0d',
              boxShadow:
                'inset 0 2px 3px rgba(255,255,255,.1), inset 0 -2px 4px rgba(0,0,0,.35), 0 3px 6px rgba(0,0,0,.45)',
              textShadow: '0 1px 1px rgba(0,0,0,.45)',
            }}
          >
            A
          </button>
        </div>
      </div>
    </div>
  );
}
