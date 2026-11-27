/*
 * ------------------------------------------------------------------------
 * 名称：input-event.hpp
 * 说明：硬件契约：用户输入事件。界面把键盘、鼠标、游戏手柄的原始事件交给核心：键盘与鼠标交给主机型号自己处理，游戏手柄与（不属于机器的）键盘按键经手柄绑定变成手柄口的按键。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * 备注：事件只描述用户做了什么，不带“这是哪个手柄键”的含义；游戏手柄事件只给手柄绑定（见 contracts/runtime/input-hook.hpp），不会交给 Machine::input。
 *       键盘用物理键位（KeyboardEvent.code），鼠标的位置是画面（256x240 的标准帧）上的坐标，游戏手柄是浏览器标准映射的快照。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace ember::contracts::hardware {

/** 键盘：一个物理键按下或松开。 */
struct KeyInput {
  /** 物理键位，取 KeyboardEvent.code，如 KeyW、Space、Numpad1。 */
  std::string code;
  /** true 为按下，false 为松开。 */
  bool down{false};
};

/** 鼠标事件的种类。 */
enum class MouseKind : std::uint8_t { Move, Down, Up, Wheel, Leave };

/** 鼠标：位置、按键与滚轮。 */
struct MouseInput {
  MouseKind kind{MouseKind::Move};
  /** 画面坐标，单位是标准帧（256x240）的像素；指针在画面之外时可以超出这个范围。 */
  double x{0};
  double y{0};
  /** 相对上一次事件的位移，单位同上。 */
  double dx{0};
  double dy{0};
  /** 当前按住的按键位：1 左键、2 右键、4 中键。 */
  std::uint8_t buttons{0};
  /** 滚轮的滚动量，向下为正；只有 Wheel 事件带有。 */
  double wheel{0};
};

/** 游戏手柄：一个手柄当前的全部状态。 */
struct GamepadInput {
  /** 手柄编号，同浏览器 Gamepad API 的 index。 */
  std::uint8_t index{0};
  /** 是否连着；拔掉时为 false，按键与轴为空。 */
  bool connected{false};
  /** 各按键是否按下，顺序同浏览器的标准映射。 */
  std::vector<bool> buttons;
  /** 各轴的值，范围 -1 到 1，顺序同浏览器的标准映射。 */
  std::vector<double> axes;
  /** 设备标识，同 Gamepad.id（含设备名）；手柄绑定按它认出是哪个手柄。 */
  std::string id;
};

/** 一个输入事件。 */
using InputEvent = std::variant<KeyInput, MouseInput, GamepadInput>;

}  // namespace ember::contracts::hardware
