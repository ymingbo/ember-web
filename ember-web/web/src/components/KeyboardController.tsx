import { useEffect, useCallback } from 'react';
import type { PadButton } from '../types/ember';

interface KeyboardControllerProps {
  port: 1 | 2;
  onButtonDown: (port: 1 | 2, button: PadButton) => void;
  onButtonUp: (port: 1 | 2, button: PadButton) => void;
  enabled?: boolean;
  /**
   * 学习机模式：将所有键盘事件直接转发给模拟器。
   * 为 true 时，同时也会处理手柄映射（FC 游戏在学习机上也能玩）。
   */
  learningMachineMode?: boolean;
  /** 学习机模式下的按键回调，参数为 KeyboardEvent.code 和是否按下 */
  onSendKey?: (code: string, down: boolean) => boolean;
}

/**
 * 键盘手柄映射：
 *   端口 1（玩家 1）：
 *     方向键 = 上下左右
 *     Z = A, X = B
 *     Shift = SELECT, Enter = START
 *
 *   端口 2（玩家 2）：
 *     WASD = 上下左右
 *     J = A, K = B
 *     U = SELECT, I = START
 *
 * v3：新增学习机模式——所有按键直接透传给模拟器键盘接口。
 */
const KEY_MAP_PORT1: Record<string, PadButton> = {
  ArrowUp: 'UP',
  ArrowDown: 'DOWN',
  ArrowLeft: 'LEFT',
  ArrowRight: 'RIGHT',
  KeyZ: 'A',
  KeyX: 'B',
  ShiftLeft: 'SELECT',
  ShiftRight: 'SELECT',
  Enter: 'START',
};

const KEY_MAP_PORT2: Record<string, PadButton> = {
  KeyW: 'UP',
  KeyS: 'DOWN',
  KeyA: 'LEFT',
  KeyD: 'RIGHT',
  KeyJ: 'A',
  KeyK: 'B',
  KeyU: 'SELECT',
  KeyI: 'START',
};

export function KeyboardController({
  port,
  onButtonDown,
  onButtonUp,
  enabled = true,
  learningMachineMode = false,
  onSendKey,
}: KeyboardControllerProps) {
  const keyMap = port === 1 ? KEY_MAP_PORT1 : KEY_MAP_PORT2;

  const handleKeyDown = useCallback(
    (e: KeyboardEvent) => {
      if (!enabled) return;

      // 学习机模式：所有按键直接透传
      if (learningMachineMode && onSendKey) {
        // 阻止常见的页面滚动等默认行为（方向键、空格等）
        const preventCodes = [
          'ArrowUp', 'ArrowDown', 'ArrowLeft', 'ArrowRight',
          'Space', 'Tab', 'Enter', 'Backspace',
        ];
        if (preventCodes.includes(e.code)) {
          e.preventDefault();
        }
        onSendKey(e.code, true);
        // 同时也检查是否是手柄按键，保持兼容
        if (e.code in keyMap) {
          onButtonDown(port, keyMap[e.code]);
        }
        return;
      }

      // 普通 FC 模式：只处理手柄映射
      if (e.code in keyMap) {
        e.preventDefault();
        const button = keyMap[e.code];
        onButtonDown(port, button);
      }
    },
    [enabled, keyMap, onButtonDown, port, learningMachineMode, onSendKey],
  );

  const handleKeyUp = useCallback(
    (e: KeyboardEvent) => {
      if (!enabled) return;

      // 学习机模式：所有按键直接透传
      if (learningMachineMode && onSendKey) {
        const preventCodes = [
          'ArrowUp', 'ArrowDown', 'ArrowLeft', 'ArrowRight',
          'Space', 'Tab', 'Enter', 'Backspace',
        ];
        if (preventCodes.includes(e.code)) {
          e.preventDefault();
        }
        onSendKey(e.code, false);
        if (e.code in keyMap) {
          onButtonUp(port, keyMap[e.code]);
        }
        return;
      }

      // 普通 FC 模式
      if (e.code in keyMap) {
        e.preventDefault();
        const button = keyMap[e.code];
        onButtonUp(port, button);
      }
    },
    [enabled, keyMap, onButtonUp, port, learningMachineMode, onSendKey],
  );

  useEffect(() => {
    window.addEventListener('keydown', handleKeyDown);
    window.addEventListener('keyup', handleKeyUp);
    return () => {
      window.removeEventListener('keydown', handleKeyDown);
      window.removeEventListener('keyup', handleKeyUp);
    };
  }, [handleKeyDown, handleKeyUp]);

  return null; // 无 UI，只是事件监听
}
