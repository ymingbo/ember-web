/*
 * ------------------------------------------------------------------------
 * 名称：input-binding-registry.cpp
 * 说明：手柄绑定服务的实现。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-15
 * ------------------------------------------------------------------------
 */

#include "infra/input/input-binding-registry.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

#include "contracts/input/generated/input.hpp"
#include "contracts/input/input-error.hpp"
#include "shared/errors/core-error.hpp"

namespace ember::infra::input {

namespace {

namespace api = contracts::input;
namespace fs = std::filesystem;

/** 文件格式的版本。 */
constexpr int FILE_VERSION = 1;
/** 物理键位的最大长度与实体手柄的编号上限。 */
constexpr std::size_t MAX_KEY_CODE_LENGTH = 32;
constexpr std::int64_t MAX_BUTTON_INDEX = 31;
constexpr std::int64_t MAX_AXIS_INDEX = 15;
constexpr std::int64_t MAX_OCCURRENCE = 15;
constexpr std::size_t MAX_GAMEPAD_ID_LENGTH = 256;

bool isButtonName(const std::string& name) {
  return std::any_of(api::PAD_BUTTON_NAMES.begin(), api::PAD_BUTTON_NAMES.end(), [&](std::string_view known) { return known == name; });
}

/** 检查一个槽位；seenKeys 与 seenPads 在两个槽位之间共用，用来发现同一个物理键或同一个手柄被绑定了两次。 */
bool validateSlot(const api::PadSlot& slot, std::size_t number, std::set<std::string>& seenKeys, std::set<std::pair<std::string, std::int64_t>>& seenPads, std::string& reason) {
  const std::string where = "玩家 " + std::to_string(number);
  std::set<std::string> buttons;
  for (const auto& key : slot.keys) {
    if (!isButtonName(key.button)) {
      reason = where + "：不认识的手柄键 " + key.button;
      return false;
    }
    if (!buttons.insert(key.button).second) {
      reason = where + "：键盘上同一个手柄键绑定了多次：" + key.button;
      return false;
    }
    const bool plain = !key.code.empty() && key.code.size() <= MAX_KEY_CODE_LENGTH && std::all_of(key.code.begin(), key.code.end(), [](char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0; });
    if (!plain) {
      reason = where + "：物理键位不合格：" + key.code;
      return false;
    }
    if (!seenKeys.insert(key.code).second) {
      reason = where + "：键盘上的 " + key.code + " 已经绑定给了别的手柄键";
      return false;
    }
  }
  if (!slot.gamepad.has_value()) return true;
  const auto& pad = *slot.gamepad;
  if (pad.id.empty() || pad.id.size() > MAX_GAMEPAD_ID_LENGTH || pad.occurrence < 0 || pad.occurrence > MAX_OCCURRENCE) {
    reason = where + "：手柄的设备标识不合格";
    return false;
  }
  if (!seenPads.emplace(pad.id, pad.occurrence).second) {
    reason = where + "：这个手柄已经分给了别的玩家";
    return false;
  }
  buttons.clear();
  for (const auto& binding : pad.bindings) {
    if (!isButtonName(binding.button)) {
      reason = where + "：不认识的手柄键 " + binding.button;
      return false;
    }
    if (!buttons.insert(binding.button).second) {
      reason = where + "：实体手柄上同一个手柄键绑定了多次：" + binding.button;
      return false;
    }
    if (binding.kind == "button") {
      if (binding.index < 0 || binding.index > MAX_BUTTON_INDEX) {
        reason = where + "：按钮编号超出范围";
        return false;
      }
    } else if (binding.kind == "axis") {
      if (binding.index < 0 || binding.index > MAX_AXIS_INDEX || !binding.direction.has_value() || (*binding.direction != "negative" && *binding.direction != "positive")) {
        reason = where + "：轴的编号或方向不合格";
        return false;
      }
    } else {
      reason = where + "：不认识的绑定来源 " + binding.kind;
      return false;
    }
  }
  return true;
}

}  // namespace

InputBindingRegistry::InputBindingRegistry(InputBindingRegistryOptions options) : options_(std::move(options)), current_(std::make_shared<const api::PadSlots>(api::defaultPadSlots())) { load(); }

bool InputBindingRegistry::validate(const api::PadSlots& slots, std::string& reason) {
  if (slots.size() != api::PAD_SLOT_COUNT) {
    reason = "玩家槽位必须正好有 " + std::to_string(api::PAD_SLOT_COUNT) + " 个";
    return false;
  }
  std::set<std::string> seenKeys;
  std::set<std::pair<std::string, std::int64_t>> seenPads;
  for (std::size_t index = 0; index < slots.size(); ++index) {
    if (!validateSlot(slots[index], index + 1, seenKeys, seenPads, reason)) return false;
  }
  return true;
}

std::shared_ptr<const api::PadSlots> InputBindingRegistry::bindings() const {
  const std::lock_guard lock(mutex_);
  return current_;
}

bool InputBindingRegistry::assign(api::PadSlots slots) {
  if (nlohmann::json(slots) == nlohmann::json(*current_)) return false;
  current_ = std::make_shared<const api::PadSlots>(std::move(slots));
  save();
  return true;
}

std::shared_ptr<const api::PadSlots> InputBindingRegistry::set(api::PadSlots slots) {
  std::string reason;
  if (!validate(slots, reason)) throw shared::CoreError(api::ERROR_INVALID_BINDINGS, reason);
  std::shared_ptr<const api::PadSlots> result;
  {
    const std::lock_guard lock(mutex_);
    if (!assign(std::move(slots))) return current_;
    result = current_;
  }
  announce(result);
  return result;
}

std::shared_ptr<const api::PadSlots> InputBindingRegistry::reset() {
  std::shared_ptr<const api::PadSlots> result;
  {
    const std::lock_guard lock(mutex_);
    if (!assign(api::defaultPadSlots())) return current_;
    result = current_;
  }
  announce(result);
  return result;
}

void InputBindingRegistry::announce(const std::shared_ptr<const api::PadSlots>& slots) const {
  if (options_.events == nullptr) return;
  api::InputBindingsChangedData data;
  data.slots = *slots;
  options_.events->publish(api::INPUT_BINDINGS_CHANGED_EVENT, nlohmann::json(data));
}

void InputBindingRegistry::load() {
  if (options_.file.empty()) return;
  std::error_code error;
  if (!fs::exists(options_.file, error)) return;
  std::ifstream stream(options_.file, std::ios::binary);
  if (!stream) {
    logError("input.fileUnreadable", "无法读取手柄绑定文件，已使用默认绑定", error.message());
    return;
  }
  const std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
  const auto document = nlohmann::json::parse(text, nullptr, false);
  if (!document.is_object() || document.value("version", 0) != FILE_VERSION || !document.contains("slots") || !document["slots"].is_array()) {
    logError("input.fileInvalid", "手柄绑定文件的格式不对，已使用默认绑定", "");
    return;
  }
  try {
    api::PadSlots slots;
    for (std::size_t index = 0; index < document["slots"].size(); ++index) slots.push_back(api::parsePadSlot(document["slots"][index], "slots[" + std::to_string(index) + "]"));
    std::string reason;
    if (!validate(slots, reason)) {
      logError("input.fileInvalid", "手柄绑定文件里的绑定不合格，已使用默认绑定", reason);
      return;
    }
    current_ = std::make_shared<const api::PadSlots>(std::move(slots));
  } catch (const std::exception& exception) {
    logError("input.fileInvalid", "手柄绑定文件里的绑定不合格，已使用默认绑定", exception.what());
  }
}

void InputBindingRegistry::save() const {
  if (options_.file.empty()) return;
  const nlohmann::json document = {{"version", FILE_VERSION}, {"slots", nlohmann::json(*current_)}};
  try {
    std::error_code error;
    fs::create_directories(options_.file.parent_path(), error);
    if (error) throw std::runtime_error("无法创建目录：" + error.message());
    fs::path temporary = options_.file;
    temporary += ".tmp";
    {
      std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
      const std::string text = document.dump(2);
      stream.write(text.data(), static_cast<std::streamsize>(text.size()));
      stream.flush();
      if (!stream) throw std::runtime_error("无法写入临时文件");
    }
    fs::rename(temporary, options_.file, error);
    if (error) {
      fs::remove(temporary, error);
      throw std::runtime_error("无法替换手柄绑定文件");
    }
  } catch (const std::exception& exception) {
    logError("input.saveFailed", "无法保存手柄绑定，绑定只在本次运行里有效", exception.what());
  }
}

void InputBindingRegistry::logError(const std::string& code, const std::string& message, const std::string& reason) const {
  if (options_.errors == nullptr) return;
  options_.errors->write({"input", code, message, {{"reason", reason}}});
}

}  // namespace ember::infra::input
