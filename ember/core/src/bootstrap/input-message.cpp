/*
 * ------------------------------------------------------------------------
 * 名称：input-message.cpp
 * 说明：输入消息的解析实现。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * ------------------------------------------------------------------------
 */

#include "bootstrap/input-message.hpp"

#include <cctype>
#include <cstdint>

#include <nlohmann/json.hpp>

#include "shared/errors/core-error.hpp"
#include "shared/json/json-fields.hpp"

namespace ember::bootstrap {

namespace hw = contracts::hardware;
namespace fields = shared::json_fields;

namespace {

/** 键位名的最大长度。 */
constexpr std::size_t MAX_KEY_CODE_LENGTH = 32;
/** 鼠标坐标、位移与滚轮量的绝对值上限。 */
constexpr double MAX_MOUSE_VALUE = 1.0e6;
/** 鼠标按键位的最大值：左、右、中键三位。 */
constexpr std::int64_t MAX_MOUSE_BUTTONS = 7;
/** 游戏手柄的最大编号、按键数与轴数。 */
constexpr std::int64_t MAX_GAMEPAD_INDEX = 15;
constexpr std::size_t MAX_GAMEPAD_BUTTONS = 32;
constexpr std::size_t MAX_GAMEPAD_AXES = 16;
/** 手柄设备标识的最大长度（字节）。 */
constexpr std::size_t MAX_GAMEPAD_ID_LENGTH = 256;

const nlohmann::json& required(const nlohmann::json& object, const char* name) {
  const nlohmann::json* value = fields::find(object, name);
  if (value == nullptr) throw shared::SchemaError(std::string("缺少字段 ") + name);
  return *value;
}

hw::KeyInput readKey(const nlohmann::json& message) {
  fields::rejectUnknown(message, "输入消息", {"type", "code", "down"});
  hw::KeyInput key;
  key.code = fields::readString(required(message, "code"), "code", 1, MAX_KEY_CODE_LENGTH, {});
  for (const char c : key.code) {
    if (!std::isalnum(static_cast<unsigned char>(c))) throw shared::SchemaError("code 只能是字母与数字");
  }
  key.down = fields::readBoolean(required(message, "down"), "down");
  return key;
}

hw::MouseInput readMouse(const nlohmann::json& message) {
  fields::rejectUnknown(message, "输入消息", {"type", "kind", "x", "y", "dx", "dy", "buttons", "wheel"});
  hw::MouseInput mouse;
  const std::string kind = fields::readString(required(message, "kind"), "kind", 1, 8, {"move", "down", "up", "wheel", "leave"});
  if (kind == "move") mouse.kind = hw::MouseKind::Move;
  else if (kind == "down") mouse.kind = hw::MouseKind::Down;
  else if (kind == "up") mouse.kind = hw::MouseKind::Up;
  else if (kind == "wheel") mouse.kind = hw::MouseKind::Wheel;
  else mouse.kind = hw::MouseKind::Leave;
  mouse.x = fields::readNumber(required(message, "x"), "x", -MAX_MOUSE_VALUE, MAX_MOUSE_VALUE);
  mouse.y = fields::readNumber(required(message, "y"), "y", -MAX_MOUSE_VALUE, MAX_MOUSE_VALUE);
  mouse.dx = fields::readNumber(required(message, "dx"), "dx", -MAX_MOUSE_VALUE, MAX_MOUSE_VALUE);
  mouse.dy = fields::readNumber(required(message, "dy"), "dy", -MAX_MOUSE_VALUE, MAX_MOUSE_VALUE);
  mouse.buttons = static_cast<std::uint8_t>(fields::readInteger(required(message, "buttons"), "buttons", 0, MAX_MOUSE_BUTTONS));
  mouse.wheel = fields::readNumber(required(message, "wheel"), "wheel", -MAX_MOUSE_VALUE, MAX_MOUSE_VALUE);
  return mouse;
}

hw::GamepadInput readGamepad(const nlohmann::json& message) {
  fields::rejectUnknown(message, "输入消息", {"type", "index", "id", "connected", "buttons", "axes"});
  hw::GamepadInput pad;
  pad.index = static_cast<std::uint8_t>(fields::readInteger(required(message, "index"), "index", 0, MAX_GAMEPAD_INDEX));
  pad.id = fields::readString(required(message, "id"), "id", 0, MAX_GAMEPAD_ID_LENGTH, {});
  pad.connected = fields::readBoolean(required(message, "connected"), "connected");
  const auto buttons = fields::readArray<bool>(required(message, "buttons"), "buttons", std::nullopt, MAX_GAMEPAD_BUTTONS,
                                               [](const nlohmann::json& item, const std::string& path) { return fields::readBoolean(item, path); });
  pad.buttons.assign(buttons.begin(), buttons.end());
  pad.axes = fields::readArray<double>(required(message, "axes"), "axes", std::nullopt, MAX_GAMEPAD_AXES,
                                       [](const nlohmann::json& item, const std::string& path) { return fields::readNumber(item, path, -1.0, 1.0); });
  return pad;
}

}  // namespace

hw::InputEvent parseInputMessage(const std::string& messageJson) {
  const auto message = nlohmann::json::parse(messageJson, nullptr, false);
  fields::requireObject(message, "输入消息");
  const std::string type = fields::readString(required(message, "type"), "type", 1, 8, {"key", "mouse", "gamepad"});
  if (type == "key") return readKey(message);
  if (type == "mouse") return readMouse(message);
  return readGamepad(message);
}

}  // namespace ember::bootstrap
