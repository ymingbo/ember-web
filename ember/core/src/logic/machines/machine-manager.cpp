/*
 * ------------------------------------------------------------------------
 * 名称：machine-manager.cpp
 * 说明：机型服务的实现。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-15
 * ------------------------------------------------------------------------
 */

#include "logic/machines/machine-manager.hpp"

#include <algorithm>
#include <set>
#include <utility>

#include "contracts/machines/machines-error.hpp"
#include "logic/machines/known-roms.hpp"
#include "shared/errors/core-error.hpp"
#include "shared/hash/crc32.hpp"

namespace ember::logic::machines {

namespace api = contracts::machines;

namespace {

/** sb97 的 ROM：1024KB。 */
constexpr std::size_t SB97_PRG_SIZE = 1024 * 1024;
/** sb2000 的 ROM：512KB。 */
constexpr std::size_t SB2000_PRG_SIZE = 512 * 1024;
/** 步步高软驱一号与步步高 98 的 ROM：128KB、2048KB。 */
constexpr std::size_t BBG1_PRG_SIZE = 128 * 1024;
constexpr std::size_t BBG98_PRG_SIZE = 2048 * 1024;
/** 金字塔 PEC-9588 与 PEC-586(686) 的 ROM：512KB。 */
constexpr std::size_t PYRAMID_PRG_SIZE = 512 * 1024;
/** 裕兴的 ROM：1024KB（V8.0-C、V8.2-D、V8.3-D、V9.0-98 等学习卡都是这个大小）。 */
constexpr std::size_t YUXING_PRG_SIZE = 1024 * 1024;
/** 中华学习机 CEC-I 的 ROM：64KB（U7 与 U35 两块 32KB 的芯片依次拼成），后面可以再接 256KB 的汉字字库。 */
constexpr std::size_t CEC1_PRG_SIZE = 64 * 1024;
constexpr std::size_t CEC1_LIBRARY_SIZE = 256 * 1024;

/** 路径里最后一段（文件名）；两种分隔符都认。 */
std::string baseName(const std::string& path) {
  const auto position = path.find_last_of("/\\");
  return position == std::string::npos ? path : path.substr(position + 1);
}

}  // namespace

MachineManager::MachineManager(std::shared_ptr<api::RomStorage> storage, std::shared_ptr<contracts::runtime::RuntimeService> runtime, std::shared_ptr<platform::EventSink> events)
    : storage_(std::move(storage)),
      runtime_(std::move(runtime)),
      events_(std::move(events)),
      entries_{{"fc", "经典 FC", false, true, 0},
               {"sb97", "小霸王 97", true, true, SB97_PRG_SIZE},
               {"sb2000", "小霸王 2000", true, true, SB2000_PRG_SIZE},
               {"bbg1", "步步高 软驱一号", true, true, BBG1_PRG_SIZE},
               {"bbg98", "步步高 98", true, true, BBG98_PRG_SIZE},
               {"pec9588", "金字塔 PEC-9588", true, true, PYRAMID_PRG_SIZE},
               {"pec586", "金字塔 PEC-586(686)", true, true, PYRAMID_PRG_SIZE},
               {"yuxing", "裕兴", true, true, YUXING_PRG_SIZE},
               {"cec1", "中华学习机 CEC-I", true, true, CEC1_PRG_SIZE, CEC1_LIBRARY_SIZE}} {}

const MachineManager::Entry& MachineManager::entry(const std::string& id) const {
  for (const Entry& candidate : entries_) {
    if (candidate.id == id) return candidate;
  }
  throw shared::CoreError(api::ERROR_NOT_FOUND, "没有这个机型：" + id);
}

api::MachineInfo MachineManager::info(const Entry& item) const {
  api::MachineInfo result;
  result.id = item.id;
  result.name = item.name;
  result.needsRom = item.needsRom;
  result.supported = item.supported;
  if (item.needsRom) {
    if (const auto stored = storage_->find(item.id)) result.rom = api::RomFile{stored->fileName, stored->size, labelOf(item, *stored)};
  }
  return result;
}

std::string MachineManager::labelOf(const Entry& item, const api::StoredRom& stored) const {
  try {
    const std::vector<std::uint8_t> rom = storage_->load(item.id);
    checkRom(item, rom);
    return romLabel(shared::crc32(rom), stored.fileName);
  } catch (const shared::CoreError&) {
    // 保存的文件读不出或已经不合格时不让清单失败，只显示文件名。
    return stored.fileName;
  }
}

void MachineManager::checkRom(const Entry& item, const std::vector<std::uint8_t>& file) {
  if (file.size() == item.prgSize || (item.optionalSize != 0 && file.size() == item.prgSize + item.optionalSize)) return;
  std::string accepted = std::to_string(item.prgSize / 1024) + "KB";
  if (item.optionalSize != 0) accepted += " 或 " + std::to_string((item.prgSize + item.optionalSize) / 1024) + "KB";
  throw shared::CoreError(api::ERROR_ROM_INVALID, item.name + " 的 ROM 必须是 " + accepted + " 的 ROM 文件，这个文件是 " + std::to_string(file.size()) + " 字节");
}

api::MachinesListResult MachineManager::list() const {
  api::MachinesListResult result;
  for (const Entry& candidate : entries_) result.machines.push_back(info(candidate));
  return result;
}

api::MachinesSetRomResult MachineManager::setRom(const api::MachinesSetRomParams& params) {
  const Entry& target = entry(params.machine);
  if (!target.needsRom) throw shared::CoreError(api::ERROR_ROM_NOT_NEEDED, target.name + " 不需要 ROM");
  if (!target.supported) throw shared::CoreError(api::ERROR_UNSUPPORTED, target.name + " 暂未支持");
  const std::vector<std::uint8_t> file = storage_->readSource(params.path);
  checkRom(target, file);
  storage_->store(target.id, baseName(params.path), file);
  events_->publish(api::MACHINES_CHANGED_EVENT, nlohmann::json(api::MachinesChangedData{target.id}));
  return api::MachinesSetRomResult{info(target)};
}

api::MachinesSetRomsResult MachineManager::setRoms(const api::MachinesSetRomsParams& params) {
  api::MachinesSetRomsResult result;
  std::set<std::string> addedMachines;
  for (const std::string& path : storage_->listSources(params.path)) {
    const std::string fileName = baseName(path);
    const auto skip = [&](const char* reason) { result.skipped.push_back(api::RomSkipped{fileName, reason}); };
    std::vector<std::uint8_t> file;
    try {
      file = storage_->readSource(path);
    } catch (const shared::CoreError&) {
      // 读不出或大得不像 ROM 的文件，当作认不出。
      skip("unrecognized");
      continue;
    }
    const auto known = findKnownRom(shared::crc32(file));
    if (!known) {
      skip("unrecognized");
      continue;
    }
    const Entry& target = entry(std::string(known->machine));
    if (addedMachines.count(target.id) != 0) {
      skip("duplicate");
      continue;
    }
    if (storage_->find(target.id)) {
      skip("exists");
      continue;
    }
    storage_->store(target.id, fileName, file);
    addedMachines.insert(target.id);
    events_->publish(api::MACHINES_CHANGED_EVENT, nlohmann::json(api::MachinesChangedData{target.id}));
    result.added.push_back(info(target));
  }
  return result;
}

api::MachinesRemoveRomResult MachineManager::removeRom(const api::MachinesRemoveRomParams& params) {
  const Entry& target = entry(params.machine);
  if (!target.needsRom) throw shared::CoreError(api::ERROR_ROM_NOT_NEEDED, target.name + " 不需要 ROM");
  const bool had = storage_->find(target.id).has_value();
  storage_->remove(target.id);
  if (had) events_->publish(api::MACHINES_CHANGED_EVENT, nlohmann::json(api::MachinesChangedData{target.id}));
  return api::MachinesRemoveRomResult{info(target)};
}

std::optional<contracts::runtime::MachineRom> MachineManager::currentRom(const std::string& machine) const {
  const Entry& target = entry(machine);
  const auto stored = target.needsRom ? storage_->find(target.id) : std::nullopt;
  if (!stored) return std::nullopt;
  contracts::runtime::MachineRom result;
  result.rom = storage_->load(target.id);
  checkRom(target, result.rom);
  result.label = romLabel(shared::crc32(result.rom), stored->fileName);
  return result;
}

api::MachinesBootResult MachineManager::boot(const api::MachinesBootParams& params) {
  const Entry& target = entry(params.machine);
  if (!target.needsRom) throw shared::CoreError(api::ERROR_CARTRIDGE_REQUIRED, target.name + " 开机必须有 NES 文件，请改用打开卡带");
  if (!target.supported) throw shared::CoreError(api::ERROR_UNSUPPORTED, target.name + " 暂未支持");
  if (!storage_->find(target.id)) throw shared::CoreError(api::ERROR_NO_ROM, target.name + " 还没有添加 ROM，请先在“设置→机型与 ROM”里添加");
  contracts::runtime::MachineBoot request;
  request.machine = target.id;
  request.rom = storage_->load(target.id);
  checkRom(target, request.rom);
  request.romLabel = romLabel(shared::crc32(request.rom), storage_->find(target.id)->fileName);
  request.powerOn = params.powerOn.value_or(true);
  request.resume = params.resume.value_or(true);
  const contracts::runtime::RuntimeState state = runtime_->bootMachine(std::move(request));
  return api::MachinesBootResult{info(target), state};
}

}  // namespace ember::logic::machines
