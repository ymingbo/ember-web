/*
 * ------------------------------------------------------------------------
 * 名称：mapper-chip.cpp
 * 说明：由插件创建的卡带的实现：存储区、窗口与译码表、名称表连法、脚本回调与宿主对象。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * ------------------------------------------------------------------------
 */

#include "infra/mappers/mapper-chip.hpp"

#include <algorithm>
#include <span>
#include <utility>

#include <nlohmann/json.hpp>

#include "contracts/mappers/mapper-error.hpp"
#include "shared/errors/core-error.hpp"

namespace ember::infra::mappers {

namespace {

namespace hw = contracts::hardware;
namespace mapper_error = contracts::mappers;

/** 脚本的 onA12：A12 要先连续低过这么多次显存访问，下一次变高才算一次上升沿。 */
constexpr unsigned A12_LOW_ACCESSES = 4;

/** 一条脚本日志的最大字节数，保留的最大条数。 */
constexpr std::size_t MAX_LOG_BYTES = 2000;
constexpr std::size_t MAX_LOG_ENTRIES = 200;
/** ctx.timer 同时可用的计时名字个数上限。 */
constexpr std::size_t MAX_TIMER_NAMES = 32;
/** CPU 总线 64K，PPU 侧只有图案表 8K。 */
constexpr std::size_t CPU_SLOT_COUNT = 0x10000;
constexpr std::size_t PPU_SLOT_COUNT = 0x2000;

/** 文件头里的镜像方式对应的名称表连法。 */
NametableMode fromHeader(mapper_error::Mirroring mirroring) {
  switch (mirroring) {
    case mapper_error::Mirroring::Vertical:
      return NametableMode::Vertical;
    case mapper_error::Mirroring::FourScreen:
      return NametableMode::Four;
    case mapper_error::Mirroring::Horizontal:
      break;
  }
  return NametableMode::Horizontal;
}

/** ctx.header 的 JSON：规范化的卡带描述；文件头没写明的 RAM 大小为 null。 */
std::string headerJson(const mapper_error::CartridgeDescription& description) {
  const auto sizeOrNull = [](const std::optional<std::uint32_t>& size) { return size ? nlohmann::json(*size) : nlohmann::json(nullptr); };
  return nlohmann::json{{"mapper", description.mapper},
                        {"variant", description.variant},
                        {"prgSize", description.prgSize},
                        {"chrSize", description.chrSize},
                        {"mirroring", std::string(mapper_error::mirroringName(description.mirroring))},
                        {"battery", description.battery},
                        {"trainer", description.trainer},
                        {"crc32Prg", description.crc32Prg},
                        {"crc32All", description.crc32All},
                        {"nes20", description.nes20},
                        {"prgRamSize", sizeOrNull(description.prgRamSize)},
                        {"prgNvramSize", sizeOrNull(description.prgNvramSize)},
                        {"chrRamSize", sizeOrNull(description.chrRamSize)},
                        {"chrNvramSize", sizeOrNull(description.chrNvramSize)}}
      .dump();
}

/** RAM 存储区的实际大小：取自文件头的按 HeaderSize 的规则算，否则是清单里写的固定大小。 */
std::uint32_t resolveRamSize(const std::optional<HeaderSize>& header, std::uint32_t fixedSize, const mapper_error::CartridgeDescription& description) {
  if (!header) return fixedSize;
  switch (header->field) {
    case HeaderRam::PrgRam:
      return description.prgRamSize ? *description.prgRamSize : (description.battery ? 0U : header->defaultSize);
    case HeaderRam::PrgNvram:
      return description.prgNvramSize ? *description.prgNvramSize : (description.battery ? header->defaultSize : 0U);
    case HeaderRam::PrgAny: {
      const std::uint32_t saved = resolveRamSize(HeaderSize{HeaderRam::PrgNvram, header->defaultSize}, 0, description);
      return saved != 0 ? saved : resolveRamSize(HeaderSize{HeaderRam::PrgRam, header->defaultSize}, 0, description);
    }
    case HeaderRam::ChrRam:
      return description.chrRamSize ? *description.chrRamSize : header->defaultSize;
    case HeaderRam::ChrNvram:
      return description.chrNvramSize ? *description.chrNvramSize : 0U;
  }
  return fixedSize;
}

}  // namespace

MapperChip::MapperChip(std::shared_ptr<const LoadedPlugin> plugin, const contracts::mappers::CartridgeImage& image, hw::IrqOutput& irq, hw::PadSink& pads, ScriptLimits limits,
                       const contracts::hardware::CircuitCatalog* circuitCatalog)
    : plugin_(std::move(plugin)),
      pads_(pads),
      tape_(pads.tapeLine()),
      scheduler_(pads.cartridgeScheduler()),
      cpuClock_(pads.cartridgeCpuClock()),
      mixer_(irq),
      padMasks_(pads.padPortCount(), 0),
      cpuSlots_(CPU_SLOT_COUNT),
      ppuSlots_(PPU_SLOT_COUNT),
      cpuSide_(*this, false),
      ppuSide_(*this, true) {
  const Manifest& manifest = plugin_->manifest;
  scriptIrq_ = mixer_.addSource();

  // 存储区：rom 从文件复制，来源为空且有后备时改用后备的 RAM；ram 清零。
  ScriptLayout layout;
  layout.headerJson = headerJson(image.description);
  layout.padPorts = padMasks_.size();
  layout.hasTape = tape_ != nullptr;
  for (const StorageDecl& decl : manifest.storage) {
    std::unique_ptr<StorageBlock> block;
    if (decl.ram) {
      // prgAny 优先用存档 RAM（带电池），所以这种存储区是否带电池由头里的存档 RAM 决定。
      const bool battery = decl.header && decl.header->field == HeaderRam::PrgAny ? resolveRamSize(HeaderSize{HeaderRam::PrgNvram, decl.header->defaultSize}, 0, image.description) != 0 : decl.battery;
      block = std::make_unique<StorageBlock>(hw::StorageKind::Ram, std::vector<std::uint8_t>(resolveRamSize(decl.header, decl.size, image.description), 0), battery);
    } else {
      const std::vector<std::uint8_t>& source = decl.from == StorageSource::FilePrg ? image.prg : image.chr;
      if (source.empty() && decl.fallback) {
        block = std::make_unique<StorageBlock>(hw::StorageKind::Ram, std::vector<std::uint8_t>(resolveRamSize(decl.fallback->header, decl.fallback->size, image.description), 0), decl.fallback->battery);
      } else {
        block = std::make_unique<StorageBlock>(hw::StorageKind::Rom, source, false);
      }
    }
    storages_.push_back(std::move(block));
    layout.storages.push_back(decl.id);
  }

  // 窗口与译码表。
  for (std::size_t index = 0; index < manifest.windows.size(); ++index) {
    const WindowDecl& decl = manifest.windows[index];
    const auto storageIndex = [&](const std::string& id) {
      return static_cast<std::size_t>(std::find_if(manifest.storage.begin(), manifest.storage.end(), [&](const StorageDecl& item) { return item.id == id; }) - manifest.storage.begin());
    };
    Window window;
    window.first = static_cast<std::uint16_t>(decl.range.first);
    window.size = decl.size;
    window.storage = storages_[storageIndex(decl.storage)].get();
    window.initialStorage = window.storage;
    for (const std::string& choice : decl.storageChoices) window.choices.push_back(storages_[storageIndex(choice)].get());
    window.writable = decl.writable;
    window.initialWritable = decl.writable;
    window.initialKind = decl.initialKind;
    window.initialBank = decl.initialBank;
    windows_.push_back(window);
    layout.windows.push_back(decl.id);
    std::vector<Slot>& slots = decl.ppu ? ppuSlots_ : cpuSlots_;
    for (std::uint32_t address = decl.range.first; address <= decl.range.last; ++address) slots[address].window = static_cast<std::int16_t>(index);
  }
  for (std::size_t index = 0; index < manifest.registers.size(); ++index) {
    const RegisterDecl& decl = manifest.registers[index];
    std::vector<Slot>& slots = decl.ppu ? ppuSlots_ : cpuSlots_;
    for (std::uint32_t address = decl.range.first; address <= decl.range.last; ++address) {
      slots[address].reg = static_cast<std::int16_t>(index);
      slots[address].readRegister = decl.read;
      slots[address].writeRegister = decl.write;
    }
  }

  // 状态与名称表。
  for (const StateDecl& decl : manifest.state) {
    layout.states.push_back(decl.name);
    stateValues_.push_back(0);
    stateMasks_.push_back(decl.bits >= 32 ? 0xFFFFFFFFU : (std::uint32_t{1} << decl.bits) - 1U);
  }
  initialNametable_ = manifest.nametable == NametableMode::Header ? fromHeader(image.description.mirroring) : manifest.nametable;
  nametable_ = initialNametable_;
  busConflict_ = manifest.busConflict;
  for (Window& window : windows_) applyInitial(window);

  // 清单声明的寄存器、查表与磁带计时：把状态名、窗口标识、表标识解析成下标，访问时不再查名字。
  const auto stateIndex = [&](const std::string& name) {
    return static_cast<std::size_t>(std::find_if(manifest.state.begin(), manifest.state.end(), [&](const StateDecl& item) { return item.name == name; }) - manifest.state.begin());
  };
  const auto windowIndex = [&](const std::string& id) {
    return static_cast<std::size_t>(std::find_if(manifest.windows.begin(), manifest.windows.end(), [&](const WindowDecl& item) { return item.id == id; }) - manifest.windows.begin());
  };
  const auto tableIndex = [&](const std::string& id) {
    return static_cast<std::size_t>(std::find_if(manifest.tables.begin(), manifest.tables.end(), [&](const TableDecl& item) { return item.id == id; }) - manifest.tables.begin());
  };
  for (const TableDecl& decl : manifest.tables) {
    Table table;
    table.id = decl.id;
    for (const IndexSource& source : decl.index) table.sources.push_back(TableSource{source.from, source.from == IndexFrom::State ? stateIndex(source.state) : 0, source.shift, source.bits});
    table.valueMask = decl.bits >= 32 ? 0xFFFFFFFFU : (std::uint32_t{1} << decl.bits) - 1U;
    table.values.assign(std::size_t{1} << decl.indexBits, 0);
    table.known.assign(std::size_t{1} << decl.indexBits, 0);
    tables_.push_back(std::move(table));
  }
  for (const RegisterDecl& decl : manifest.registers) {
    ResolvedRegister resolved;
    resolved.readKind = decl.readAction.kind;
    resolved.readValue = decl.readAction.value;
    if (decl.readAction.kind == ReadKind::State) resolved.readState = stateIndex(decl.readAction.state);
    resolved.releaseIrq = decl.readAction.releaseIrq;
    resolved.writeScript = decl.writeAction.script;
    if (!decl.writeAction.circuit.empty()) {
      resolved.circuit = static_cast<std::size_t>(std::find_if(manifest.circuits.begin(), manifest.circuits.end(), [&](const CircuitDecl& item) { return item.id == decl.writeAction.circuit; }) - manifest.circuits.begin());
    }
    if (!decl.writeAction.state.empty()) resolved.writeState = stateIndex(decl.writeAction.state);
    for (const BankSetter& setter : decl.writeAction.banks) resolved.banks.push_back(ResolvedRegister::Bank{windowIndex(setter.window), setter.shift, setter.mask});
    if (!decl.writeAction.updateTable.empty()) {
      resolved.table = tableIndex(decl.writeAction.updateTable);
      resolved.into = stateIndex(decl.writeAction.updateInto);
    }
    registers_.push_back(std::move(resolved));
  }
  if (manifest.tape && manifest.tape->timer) {
    timerSamples_ = stateIndex(manifest.tape->timer->samples);
    timerTicks_ = stateIndex(manifest.tape->timer->ticks);
  }

  // 电路：按类型在目录里创建，接线里的窗口标识解析成下标。
  for (const CircuitDecl& decl : manifest.circuits) {
    if (circuitCatalog == nullptr || !circuitCatalog->knows(decl.type)) throw shared::CoreError(mapper_error::ERROR_INVALID_PACKAGE, "插件 " + manifest.id + " 引用了主机里没有的电路：" + decl.type);
    CircuitSlot slot;
    slot.id = decl.id;
    slot.circuit = circuitCatalog->create(decl.type, mixer_, contracts::hardware::CircuitOptions{decl.revA});
    if (decl.wire) {
      for (const std::string& id : decl.wire->prg) slot.prgWindows.push_back(windowIndex(id));
      for (const std::string& id : decl.wire->chr) slot.chrWindows.push_back(windowIndex(id));
      slot.mirroring = decl.wire->mirroring;
      if (!decl.wire->ram.empty()) slot.ramWindow = windowIndex(decl.wire->ram);
    }
    layout.circuits.push_back(decl.id);
    circuits_.push_back(std::move(slot));
  }
  for (const SettingDecl& setting : manifest.settings) {
    layout.settings.push_back(ScriptSetting{setting.key, setting.kind == SettingKind::Switch, setting.switchDefault, setting.textDefault});
  }

  // 脚本：清单里有要回调脚本的寄存器或要脚本计算的查表时必须有脚本并导出对应的函数；只有声明式寄存器的卡带可以没有脚本。
  const bool needsScript = manifest.needsReadHandler() || manifest.needsWriteHandler() || manifest.needsTables();
  if (!plugin_->script.empty() || needsScript) {
    if (plugin_->script.empty()) throw shared::CoreError(mapper_error::ERROR_INVALID_PACKAGE, "插件 " + manifest.id + " 声明了要回调脚本的寄存器或查表，但包里没有 main.js");
    ScriptBindings& bindings = *this;
    script_ = std::make_unique<ScriptRuntime>(manifest.id, plugin_->script, std::move(layout), bindings, limits);
    if (manifest.needsReadHandler() && !script_->hasRead()) throw shared::CoreError(mapper_error::ERROR_INVALID_PACKAGE, "插件 " + manifest.id + " 声明了读寄存器，但 main.js 没有导出 read");
    if (manifest.needsWriteHandler() && !script_->hasWrite()) throw shared::CoreError(mapper_error::ERROR_INVALID_PACKAGE, "插件 " + manifest.id + " 声明了写寄存器，但 main.js 没有导出 write");
    for (const Table& table : tables_) {
      if (!script_->hasTable(table.id)) throw shared::CoreError(mapper_error::ERROR_INVALID_PACKAGE, "插件 " + manifest.id + " 声明了查表 " + table.id + "，但 main.js 没有导出 tables." + table.id);
    }
  }
  // 磁带数据线的翻转在翻转的时刻通知卡带（清单声明的边沿计时与脚本的 onTapeEdge）；主机没有磁带机时不挂。
  if (tape_ != nullptr && (timerSamples_.has_value() || (script_ != nullptr && script_->hasTapeEdge()))) {
    tapeListening_ = true;
    tape_->setEdgeListener([this](bool level, std::uint32_t samples) { onTapeEdge(level, samples); });
  }
}

MapperChip::~MapperChip() {
  cancelTimers();
  if (tapeListening_) tape_->setEdgeListener({});
}

template <typename Call>
bool MapperChip::runScript(Call&& call) {
  if (failure_ || script_ == nullptr) return false;
  try {
    call();
    return true;
  } catch (const shared::CoreError& error) {
    failure_ = contracts::mappers::ScriptFailure{error.code(), plugin_->manifest.id, error.what()};
    return false;
  }
}

hw::CiramControl MapperChip::ciramControl(std::uint16_t ppuAddress) const {
  switch (nametable_) {
    case NametableMode::Horizontal:
      return hw::CiramControl{true, (ppuAddress & 0x800U) != 0};
    case NametableMode::Vertical:
      return hw::CiramControl{true, (ppuAddress & 0x400U) != 0};
    case NametableMode::Single0:
      return hw::CiramControl{true, false};
    case NametableMode::Single1:
      return hw::CiramControl{true, true};
    case NametableMode::Four:
      return hw::CiramControl{false, false};
    case NametableMode::Custom: {
      const NametableSlot& slot = customSlots_[(ppuAddress >> 10) & 3U];
      return slot.storage != nullptr ? hw::CiramControl{false, false} : hw::CiramControl{true, slot.ciramPage1};
    }
    case NametableMode::Header:
      break;
  }
  return hw::CiramControl{true, false};
}

void MapperChip::reset(bool power) {
  failure_.reset();
  cancelTimers();
  busConflict_ = plugin_->manifest.busConflict;
  if (power) powerCycle();
  applyDeclaredReset();
  if (script_ != nullptr && script_->hasReset()) runScript([&] { script_->reset(power); });
}

void MapperChip::applyDeclaredReset() {
  const Manifest& manifest = plugin_->manifest;
  for (std::size_t index = 0; index < manifest.state.size(); ++index) {
    if (manifest.state[index].initial) stateValues_[index] = *manifest.state[index].initial;
  }
  if (!manifest.tape) return;
  if (manifest.tape->timer && manifest.tape->timer->irq) mixer_.set(scriptIrq_, false);
  if (tape_ != nullptr && manifest.tape->comparator) {
    const TapeComparatorDecl& comparator = *manifest.tape->comparator;
    tape_->setComparator(hw::TapeComparator{static_cast<std::int16_t>(comparator.low), static_cast<std::int16_t>(comparator.high), comparator.invert});
  }
}

void MapperChip::input(const hw::InputEvent& event) {
  if (script_ == nullptr) return;
  if (const auto* key = std::get_if<hw::KeyInput>(&event)) {
    // 物理键的状态先更新，脚本里 ctx.keys.isDown 读到的就是这次事件之后的状态。
    if (key->down) heldKeys_.insert(key->code);
    else heldKeys_.erase(key->code);
    if (script_->hasKey()) runScript([&] { script_->key(*key); });
  } else if (const auto* mouse = std::get_if<hw::MouseInput>(&event)) {
    if (script_->hasMouse()) runScript([&] { script_->mouse(*mouse); });
  }
}

std::vector<contracts::mappers::StorageSummary> MapperChip::storageList() const {
  std::vector<contracts::mappers::StorageSummary> list;
  const auto& decls = plugin_->manifest.storage;
  for (std::size_t index = 0; index < decls.size(); ++index) {
    const StorageBlock& block = *storages_[index];
    list.push_back({decls[index].id, block.kind(), block.size(), block.battery()});
  }
  return list;
}

const hw::StorageRegion* MapperChip::storage(std::string_view id) const {
  const auto& decls = plugin_->manifest.storage;
  const auto found = std::find_if(decls.begin(), decls.end(), [&](const StorageDecl& item) { return item.id == id; });
  return found == decls.end() ? nullptr : storages_[static_cast<std::size_t>(found - decls.begin())].get();
}

bool MapperChip::pokeStorage(std::string_view id, std::size_t offset, std::uint8_t value) {
  const auto& decls = plugin_->manifest.storage;
  const auto found = std::find_if(decls.begin(), decls.end(), [&](const StorageDecl& item) { return item.id == id; });
  if (found == decls.end()) return false;
  StorageBlock& block = *storages_[static_cast<std::size_t>(found - decls.begin())];
  return offset < block.size() && block.write(offset, value);
}

std::vector<std::string> MapperChip::stateNames() const {
  std::vector<std::string> names;
  for (const StateDecl& decl : plugin_->manifest.state) names.push_back(decl.name);
  return names;
}

std::optional<std::uint32_t> MapperChip::state(std::string_view name) const {
  const auto& decls = plugin_->manifest.state;
  const auto found = std::find_if(decls.begin(), decls.end(), [&](const StateDecl& item) { return item.name == name; });
  if (found == decls.end()) return std::nullopt;
  return stateValues_[static_cast<std::size_t>(found - decls.begin())];
}

// ---------------------------------------------------------------- 总线访问

hw::BusRead MapperChip::Side::read(std::uint16_t address) { return owner_.readBus(ppu_, address); }

hw::BusRead MapperChip::Side::peek(std::uint16_t address) { return owner_.peekBus(ppu_, address); }

std::uint32_t MapperChip::randomBelow(std::uint32_t max) {
  // xorshift32：同样的上电得到同样的序列。
  randomState_ ^= randomState_ << 13;
  randomState_ ^= randomState_ >> 17;
  randomState_ ^= randomState_ << 5;
  return randomState_ % max;
}

std::optional<std::uint8_t> MapperChip::cpuPeek(std::uint16_t address) {
  // 只看窗口，不经过寄存器：读寄存器的脚本需要读到寄存器下面的 ROM 或 RAM。
  const Slot slot = cpuSlots_[address];
  if (slot.window < 0) return std::nullopt;
  const hw::BusRead result = readWindow(windows_[static_cast<std::size_t>(slot.window)], address);
  if (result.drivenMask == 0) return std::nullopt;
  return result.value;
}

hw::BusRead MapperChip::peekBus(bool ppu, std::uint16_t address) const {
  if (ppu && address >= PPU_SLOT_COUNT) return readOwnNametable(address);
  const Slot slot = ppu ? ppuSlots_[address] : cpuSlots_[address];
  if (slot.readRegister) {
    // 调试读不能有副作用：只有声明的固定值与状态可以直接给出，回调脚本的读不触发。
    const ResolvedRegister& decl = registers_[static_cast<std::size_t>(slot.reg)];
    if (decl.readKind == ReadKind::Value) return hw::driveBus(decl.readValue);
    if (decl.readKind == ReadKind::State) return hw::driveBus(static_cast<std::uint8_t>(stateValues_[decl.readState]));
    return hw::releaseBus();
  }
  return slot.window >= 0 ? readWindow(windows_[static_cast<std::size_t>(slot.window)], address) : hw::releaseBus();
}

contracts::mappers::AddressInfo MapperChip::describeAddress(bool ppu, std::uint16_t address) const {
  using Info = contracts::mappers::AddressInfo;
  Info info;
  if (ppu && address >= PPU_SLOT_COUNT) {
    if (nametable_ == NametableMode::Custom) {
      const NametableSlot& custom = customSlots_[(address >> 10) & 3U];
      if (custom.storage != nullptr && custom.storage->size() != 0) {
        info.kind = Info::Kind::Storage;
        info.offset = (custom.bank * 1024 + (address & 0x3FFU)) % custom.storage->size();
        const auto& decls = plugin_->manifest.storage;
        for (std::size_t index = 0; index < decls.size(); ++index) {
          if (storages_[index].get() == custom.storage) info.storage = decls[index].id;
        }
        info.mapped = true;
        return info;
      }
    }
    info.kind = nametable_ == NametableMode::Four ? Info::Kind::Nametable : Info::Kind::None;
    info.offset = address & 0xFFFU;
    return info;
  }
  const Slot slot = ppu ? ppuSlots_[address] : cpuSlots_[address];
  // 读寄存器会回调脚本；只写的寄存器不影响读，所以它下面有窗口时仍给出窗口的译码。
  if (slot.readRegister || (slot.writeRegister && slot.window < 0)) {
    info.kind = Info::Kind::Register;
    return info;
  }
  if (slot.window < 0) return info;
  const Window& window = windows_[static_cast<std::size_t>(slot.window)];
  const std::size_t capacity = window.storage->size();
  info.mapped = window.mapped && capacity != 0;
  if (!info.mapped) return info;
  std::size_t position = window.offset + (address - window.first);
  if (position >= capacity) position %= capacity;
  info.kind = Info::Kind::Storage;
  info.offset = position;
  const auto& decls = plugin_->manifest.storage;
  for (std::size_t index = 0; index < decls.size(); ++index) {
    if (storages_[index].get() == window.storage) info.storage = decls[index].id;
  }
  return info;
}

void MapperChip::Side::write(std::uint16_t address, std::uint8_t value) { owner_.writeBus(ppu_, address, value); }

hw::BusRead MapperChip::readBus(bool ppu, std::uint16_t address) {
  // 名称表地址只在四屏或 custom 映射到存储区时由卡带响应；图案表地址按窗口译码。
  if (ppu && address >= PPU_SLOT_COUNT) return readOwnNametable(address);
  const Slot slot = ppu ? ppuSlots_[address] : cpuSlots_[address];
  if (slot.readRegister) return readRegister(ppu, address, slot.reg);
  return slot.window >= 0 ? readWindow(windows_[static_cast<std::size_t>(slot.window)], address) : hw::releaseBus();
}

hw::BusRead MapperChip::readRegister(bool ppu, std::uint16_t address, std::int16_t index) {
  const ResolvedRegister& decl = registers_[static_cast<std::size_t>(index)];
  hw::BusRead result = hw::releaseBus();
  switch (decl.readKind) {
    case ReadKind::Script: {
      std::optional<std::uint8_t> value;
      if (runScript([&] { value = script_->read(ppu, address); }) && value) result = hw::driveBus(*value);
      break;
    }
    case ReadKind::Value:
      result = hw::driveBus(decl.readValue);
      break;
    case ReadKind::State:
      result = hw::driveBus(static_cast<std::uint8_t>(stateValues_[decl.readState]));
      break;
    case ReadKind::Open:
    case ReadKind::None:
      break;
  }
  if (decl.releaseIrq) mixer_.set(scriptIrq_, false);
  return result;
}

void MapperChip::writeBus(bool ppu, std::uint16_t address, std::uint8_t value) {
  if (ppu && address >= PPU_SLOT_COUNT) {
    writeOwnNametable(address, value);
    return;
  }
  const Slot slot = ppu ? ppuSlots_[address] : cpuSlots_[address];
  if (slot.writeRegister) {
    // 总线冲突：CPU 写入的数据与 ROM 同时驱动数据线，结果是两者相与；该地址没有映射 ROM 时不相与。
    if (busConflict_ && !ppu && slot.window >= 0) {
      const Window& window = windows_[static_cast<std::size_t>(slot.window)];
      if (window.mapped && window.storage->kind() == hw::StorageKind::Rom && window.storage->size() != 0) value &= readWindow(window, address).value;
    }
    writeRegister(ppu, address, value, slot.reg);
  } else if (slot.window >= 0) {
    writeWindow(windows_[static_cast<std::size_t>(slot.window)], address, value);
  }
}

void MapperChip::writeRegister(bool ppu, std::uint16_t address, std::uint8_t value, std::int16_t index) {
  const ResolvedRegister& decl = registers_[static_cast<std::size_t>(index)];
  if (decl.circuit) {
    writeCircuit(*decl.circuit, address, value);
    if (script_ != nullptr && script_->hasCircuit()) runScript([&] { script_->circuit(circuits_[*decl.circuit].id); });
    return;
  }
  if (decl.writeScript) {
    runScript([&] { script_->write(ppu, address, value); });
    return;
  }
  if (decl.writeState) stateSet(*decl.writeState, value);
  for (const ResolvedRegister::Bank& bank : decl.banks) windowSetBank(bank.window, (value >> bank.shift) & bank.mask);
  if (decl.table) {
    if (const auto result = lookup(*decl.table, address, value)) stateSet(decl.into, *result);
  }
}

std::optional<std::uint32_t> MapperChip::lookup(std::size_t tableNumber, std::uint16_t address, std::uint8_t value) {
  Table& table = tables_[tableNumber];
  std::array<std::uint32_t, MAX_INDEX_SOURCES> parts{};
  std::size_t index = 0;
  for (std::size_t position = 0; position < table.sources.size(); ++position) {
    const TableSource& source = table.sources[position];
    std::uint32_t part = 0;
    switch (source.from) {
      case IndexFrom::State:
        part = stateValues_[source.state];
        break;
      case IndexFrom::Address:
        part = address >> source.shift;
        break;
      case IndexFrom::Data:
        part = static_cast<std::uint32_t>(value) >> source.shift;
        break;
      case IndexFrom::Tape:
        part = tape_ != nullptr && tape_->level() ? 1U : 0U;
        break;
    }
    part &= (std::uint32_t{1} << source.bits) - 1U;
    parts[position] = part;
    index = (index << source.bits) | part;
  }
  if (table.known[index] != 0) return table.values[index];
  // 第一次遇到这个索引：调脚本计算一次并缓存（脚本的 tables 函数必须是纯函数）。
  std::optional<std::uint32_t> computed;
  if (!runScript([&] { computed = script_->table(table.id, std::span<const std::uint32_t>(parts.data(), table.sources.size())); }) || !computed) return std::nullopt;
  table.values[index] = *computed & table.valueMask;
  table.known[index] = 1;
  return table.values[index];
}

void MapperChip::onTapeEdge(bool level, std::uint32_t samples) {
  if (timerSamples_) {
    const TapeTimerDecl& timer = *plugin_->manifest.tape->timer;
    stateSet(*timerSamples_, static_cast<std::uint32_t>(std::uint64_t{stateValues_[*timerSamples_]} + samples));
    if (!level) {
      // 下降沿：把累加的采样数折成计时加进 ticks，清零采样数。
      const std::uint64_t ticks = std::uint64_t{stateValues_[*timerSamples_]} * timer.scaleNumerator / timer.scaleDenominator;
      stateSet(timerTicks_, static_cast<std::uint32_t>(std::uint64_t{stateValues_[timerTicks_]} + ticks));
      stateSet(*timerSamples_, 0);
      if (timer.irq) mixer_.set(scriptIrq_, true);
    }
  }
  if (script_ != nullptr && script_->hasTapeEdge()) runScript([&] { script_->tapeEdge(level, samples); });
}

hw::BusRead MapperChip::readWindow(const Window& window, std::uint16_t address) const {
  const std::size_t capacity = window.storage->size();
  if (!window.mapped || capacity == 0) return hw::releaseBus();
  std::size_t position = window.offset + (address - window.first);
  if (position >= capacity) position %= capacity;
  return hw::driveBus(window.storage->byteAt(position));
}

void MapperChip::writeWindow(const Window& window, std::uint16_t address, std::uint8_t value) const {
  const std::size_t capacity = window.storage->size();
  if (!window.mapped || !window.writable || capacity == 0) return;
  std::size_t position = window.offset + (address - window.first);
  if (position >= capacity) position %= capacity;
  window.storage->writeAt(position, value);
}

void MapperChip::applyInitial(Window& window) const {
  window.storage = window.initialStorage;
  window.writable = window.initialWritable;
  const std::size_t capacity = window.storage->size();
  const std::size_t banks = std::max<std::size_t>(1, capacity / window.size);
  std::size_t bank = 0;
  if (window.initialKind == InitialKind::Last) bank = banks - 1;
  else if (window.initialKind == InitialKind::Bank) {
    // 负数从末尾数（-1 是最后一个），不用取模，因为 bank 数不一定是 2 的幂。
    const auto count = static_cast<std::int64_t>(banks);
    bank = static_cast<std::size_t>(window.initialBank < 0 ? ((count + window.initialBank) % count + count) % count : window.initialBank % count);
  }
  window.offset = bank * window.size;
  window.mapped = true;
}

void MapperChip::powerCycle() {
  for (auto& block : storages_) block->powerCycle();
  for (Window& window : windows_) applyInitial(window);
  std::fill(stateValues_.begin(), stateValues_.end(), 0U);
  nametable_ = initialNametable_;
  customSlots_.fill(NametableSlot{});
  randomState_ = RANDOM_SEED;
  mixer_.clear();
  a12High_ = false;
  a12LowAccesses_ = 0;
  for (CircuitSlot& slot : circuits_) {
    slot.circuit->reset();
    applyCircuitWire(slot);
  }
}

// ---------------------------------------------------------------- 宿主对象

void MapperChip::windowSetBank(std::size_t window, std::int64_t bank) {
  if (bank < 0) throw ScriptArgumentError("bank 不能为负数");
  Window& target = windows_.at(window);
  const std::size_t banks = std::max<std::size_t>(1, target.storage->size() / target.size);
  target.offset = static_cast<std::size_t>(static_cast<std::uint64_t>(bank) % banks) * target.size;
  target.mapped = true;
}

void MapperChip::windowSetOffset(std::size_t window, std::int64_t offset) {
  if (offset < 0) throw ScriptArgumentError("偏移不能为负数");
  Window& target = windows_.at(window);
  const std::size_t capacity = target.storage->size();
  target.offset = capacity == 0 ? 0 : static_cast<std::size_t>(static_cast<std::uint64_t>(offset) % capacity);
  target.mapped = true;
}

void MapperChip::windowUnmap(std::size_t window) { windows_.at(window).mapped = false; }

std::size_t MapperChip::windowBankCount(std::size_t window) const {
  const Window& target = windows_.at(window);
  return std::max<std::size_t>(1, target.storage->size() / target.size);
}

void MapperChip::windowSelect(std::size_t window, std::size_t storage, std::int64_t bank) {
  Window& target = windows_.at(window);
  StorageBlock* chosen = storages_.at(storage).get();
  if (std::find(target.choices.begin(), target.choices.end(), chosen) == target.choices.end()) throw ScriptArgumentError("这个窗口不能切到该存储区，清单里没有列出");
  target.storage = chosen;
  windowSetBank(window, bank);
}

hw::BusRead MapperChip::readOwnNametable(std::uint16_t address) const {
  if (nametable_ == NametableMode::Four) return hw::driveBus(ownNametable_[address & 0xFFFU]);
  if (nametable_ == NametableMode::Custom) {
    const NametableSlot& slot = customSlots_[(address >> 10) & 3U];
    if (slot.storage != nullptr && slot.storage->size() != 0) return hw::driveBus(slot.storage->byteAt((slot.bank * 1024 + (address & 0x3FFU)) % slot.storage->size()));
  }
  return hw::releaseBus();
}

void MapperChip::writeOwnNametable(std::uint16_t address, std::uint8_t value) {
  if (nametable_ == NametableMode::Four) {
    ownNametable_[address & 0xFFFU] = value;
  } else if (nametable_ == NametableMode::Custom) {
    const NametableSlot& slot = customSlots_[(address >> 10) & 3U];
    if (slot.storage != nullptr && slot.storage->size() != 0) slot.storage->writeAt((slot.bank * 1024 + (address & 0x3FFU)) % slot.storage->size(), value);
  }
}

void MapperChip::nametableMap(std::size_t slot, std::string_view source, std::int64_t bank) {
  if (plugin_->manifest.nametable != NametableMode::Custom) throw ScriptArgumentError("清单的 nametable 不是 custom，不能逐个映射名称表");
  if (bank < 0) throw ScriptArgumentError("bank 不能为负数");
  NametableSlot& target = customSlots_.at(slot);
  nametable_ = NametableMode::Custom;
  if (source == "ciram0" || source == "ciram1") {
    target = NametableSlot{nullptr, 0, source == "ciram1"};
    return;
  }
  const auto& decls = plugin_->manifest.storage;
  for (std::size_t index = 0; index < decls.size(); ++index) {
    if (decls[index].id == source) {
      target = NametableSlot{storages_[index].get(), static_cast<std::size_t>(bank), false};
      return;
    }
  }
  throw ScriptArgumentError("名称表的来源必须是 'ciram0'、'ciram1' 或清单里的存储区：" + std::string(source));
}

void MapperChip::nametableSet(std::string_view mode) {
  if (mode == "horizontal") nametable_ = NametableMode::Horizontal;
  else if (mode == "vertical") nametable_ = NametableMode::Vertical;
  else if (mode == "single0") nametable_ = NametableMode::Single0;
  else if (mode == "single1") nametable_ = NametableMode::Single1;
  else if (mode == "four") nametable_ = NametableMode::Four;
  else throw ScriptArgumentError("名称表连法必须是 horizontal、vertical、single0、single1 或 four");
}

std::uint8_t MapperChip::storageRead(std::size_t storage, std::int64_t offset) {
  const StorageBlock& block = *storages_.at(storage);
  if (offset < 0 || static_cast<std::uint64_t>(offset) >= block.size()) throw ScriptArgumentError("存储区的偏移越界");
  return block.byteAt(static_cast<std::size_t>(offset));
}

bool MapperChip::storageWrite(std::size_t storage, std::int64_t offset, std::uint8_t value) {
  StorageBlock& block = *storages_.at(storage);
  if (offset < 0 || static_cast<std::uint64_t>(offset) >= block.size()) throw ScriptArgumentError("存储区的偏移越界");
  return block.writeAt(static_cast<std::size_t>(offset), value);
}

void MapperChip::log(std::string_view message) {
  if (message.size() > MAX_LOG_BYTES) {
    // 截断时退到 UTF-8 字符的边界，不留半个字符。
    std::size_t end = MAX_LOG_BYTES;
    while (end > 0 && (static_cast<unsigned char>(message[end]) & 0xC0U) == 0x80U) --end;
    message = message.substr(0, end);
  }
  log_.emplace_back(message);
  if (log_.size() > MAX_LOG_ENTRIES) log_.erase(log_.begin());
}

void MapperChip::padSet(std::size_t port, std::string_view button, bool down) {
  std::uint8_t bit = 0;
  if (button == "a") bit = hw::pad_button::A;
  else if (button == "b") bit = hw::pad_button::B;
  else if (button == "select") bit = hw::pad_button::SELECT;
  else if (button == "start") bit = hw::pad_button::START;
  else if (button == "up") bit = hw::pad_button::UP;
  else if (button == "down") bit = hw::pad_button::DOWN;
  else if (button == "left") bit = hw::pad_button::LEFT;
  else if (button == "right") bit = hw::pad_button::RIGHT;
  else throw ScriptArgumentError("按键必须是 a、b、select、start、up、down、left 或 right");
  std::uint8_t& mask = padMasks_.at(port);
  mask = down ? static_cast<std::uint8_t>(mask | bit) : static_cast<std::uint8_t>(mask & ~bit);
  pads_.setButtons(static_cast<unsigned>(port), mask);
}

void MapperChip::padRelease(std::size_t port) {
  padMasks_.at(port) = 0;
  pads_.setButtons(static_cast<unsigned>(port), 0);
}

bool MapperChip::tapeLevel() {
  if (tape_ == nullptr) throw ScriptArgumentError("当前主机没有磁带机");
  return tape_->level();
}

void MapperChip::tapeSetComparator(std::int16_t low, std::int16_t high, bool invert) {
  if (tape_ == nullptr) throw ScriptArgumentError("当前主机没有磁带机");
  tape_->setComparator(hw::TapeComparator{low, high, invert});
}

std::uint64_t MapperChip::cycle() {
  if (scheduler_ == nullptr) throw ScriptArgumentError("当前主机没有调度器");
  return static_cast<std::uint64_t>(cpuClock_.toCycles(scheduler_->now()));
}

void MapperChip::timerAfter(std::string_view name, std::int64_t cycles) {
  if (scheduler_ == nullptr) throw ScriptArgumentError("当前主机没有调度器");
  auto found = std::find_if(timers_.begin(), timers_.end(), [&](const TimerSlot& slot) { return slot.name == name; });
  if (found == timers_.end()) {
    if (timers_.size() >= MAX_TIMER_NAMES) throw ScriptArgumentError("计时的名字太多，最多 " + std::to_string(MAX_TIMER_NAMES) + " 个");
    timers_.push_back(TimerSlot{std::string(name), hw::NO_EVENT});
    found = timers_.end() - 1;
  }
  if (found->event != hw::NO_EVENT) scheduler_->cancel(found->event);
  found->event = scheduler_->scheduleAt(scheduler_->now() + cpuClock_.toMaster(cycles), *this, static_cast<std::uint32_t>(found - timers_.begin()));
}

void MapperChip::timerCancel(std::string_view name) {
  if (scheduler_ == nullptr) throw ScriptArgumentError("当前主机没有调度器");
  const auto found = std::find_if(timers_.begin(), timers_.end(), [&](const TimerSlot& slot) { return slot.name == name; });
  if (found == timers_.end() || found->event == hw::NO_EVENT) return;
  scheduler_->cancel(found->event);
  found->event = hw::NO_EVENT;
}

void MapperChip::cancelTimers() {
  for (TimerSlot& slot : timers_) {
    if (slot.event != hw::NO_EVENT && scheduler_ != nullptr) scheduler_->cancel(slot.event);
    slot.event = hw::NO_EVENT;
  }
}

void MapperChip::onEvent(hw::MasterTime, std::uint32_t tag) {
  if (tag >= timers_.size()) return;
  timers_[tag].event = hw::NO_EVENT;
  // 名字先复制：脚本回调里可能登记新的计时，使 timers_ 变长而移动元素。
  const std::string name = timers_[tag].name;
  if (script_ != nullptr && script_->hasTimer()) runScript([&] { script_->timer(name); });
}

void MapperChip::onVideoAddress(std::uint16_t ppuAddress) {
  if (script_ != nullptr && script_->hasA12()) {
    if ((ppuAddress & 0x1000U) != 0) {
      const bool rising = !a12High_ && a12LowAccesses_ >= A12_LOW_ACCESSES;
      a12High_ = true;
      a12LowAccesses_ = 0;
      if (rising) runScript([&] { script_->a12(); });
    } else {
      a12High_ = false;
      if (a12LowAccesses_ < A12_LOW_ACCESSES) ++a12LowAccesses_;
    }
  }
  for (CircuitSlot& slot : circuits_) {
    slot.circuit->onVideoAddress(ppuAddress);
    if (slot.circuit->takeBankChange()) applyCircuitWire(slot);
  }
}

std::optional<std::uint32_t> MapperChip::circuitValue(const CircuitSlot& slot, std::string_view name) {
  for (const hw::CircuitStateField& field : slot.circuit->state()) {
    if (field.name == name) return field.value;
  }
  return std::nullopt;
}

void MapperChip::writeCircuit(std::size_t index, std::uint16_t address, std::uint8_t value) {
  CircuitSlot& slot = circuits_.at(index);
  slot.circuit->writeRegister(address, value);
  applyCircuitWire(slot);
}

void MapperChip::applyCircuitWire(const CircuitSlot& slot) {
  const hw::MapperCircuit& circuit = *slot.circuit;
  if (!slot.prgWindows.empty()) {
    const std::uint32_t step = static_cast<std::uint32_t>(circuit.bankSize(hw::BankSpace::Program, 0x8000));
    for (std::size_t index = 0; index < slot.prgWindows.size(); ++index) {
      windowSetBank(slot.prgWindows[index], circuit.bank(hw::BankSpace::Program, static_cast<std::uint16_t>(0x8000U + index * step)));
    }
  }
  if (!slot.chrWindows.empty()) {
    const std::uint32_t step = static_cast<std::uint32_t>(circuit.bankSize(hw::BankSpace::Character, 0));
    for (std::size_t index = 0; index < slot.chrWindows.size(); ++index) {
      windowSetBank(slot.chrWindows[index], circuit.bank(hw::BankSpace::Character, static_cast<std::uint16_t>(index * step)));
    }
  }
  if (slot.mirroring && nametable_ != NametableMode::Four && nametable_ != NametableMode::Custom) {
    if (const auto mirroring = circuit.mirroring()) {
      switch (*mirroring) {
        case hw::CircuitMirroring::Vertical:
          nametable_ = NametableMode::Vertical;
          break;
        case hw::CircuitMirroring::Horizontal:
          nametable_ = NametableMode::Horizontal;
          break;
        case hw::CircuitMirroring::Single0:
          nametable_ = NametableMode::Single0;
          break;
        case hw::CircuitMirroring::Single1:
          nametable_ = NametableMode::Single1;
          break;
      }
    }
  }
  if (slot.ramWindow) {
    // PRG RAM 保护位：位 7 为 0 时不响应，位 6 为 1 时只读。
    if (const auto protect = circuitValue(slot, "ramProtect")) {
      Window& window = windows_.at(*slot.ramWindow);
      window.mapped = (*protect & 0x80U) != 0;
      window.writable = window.mapped && (*protect & 0x40U) == 0 && window.initialWritable;
    }
  }
}

void MapperChip::circuitWrite(std::size_t circuit, std::uint16_t address, std::uint8_t value) { writeCircuit(circuit, address, value); }

std::uint32_t MapperChip::circuitGet(std::size_t circuit, std::string_view name) {
  if (const auto value = circuitValue(circuits_.at(circuit), name)) return *value;
  throw ScriptArgumentError("电路没有这一项状态：" + std::string(name));
}

std::uint32_t MapperChip::circuitBank(std::size_t circuit, bool character, std::uint16_t address) {
  return circuits_.at(circuit).circuit->bank(character ? hw::BankSpace::Character : hw::BankSpace::Program, address);
}

}  // namespace ember::infra::mappers
