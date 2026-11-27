/*
 * ------------------------------------------------------------------------
 * 名称：machine-manager.hpp
 * 说明：机型服务的实现：机型清单、ROM 文件的校验与保存、整机机型开机（把 ROM 交给运行服务装配主机）。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-15
 * 备注：清单里的机型顺序固定；机型的“已支持”以程序里实际有这个主机型号为准（目前 fc、sb97、sb2000、bbg1、bbg98、pec9588、pec586、yuxing 与 cec1）；
 *       ROM 是只含程序的纯 bin 文件，大小等于机型的 ROM 容量，不接受带 iNES 文件头的文件；保存时保持文件原样，开机时原样交给主机。
 *       ROM 的版本按内容的 CRC-32 对照已知 ROM 表（known-roms）认出，从目录添加也靠它认出机型。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "contracts/machines/rom-storage.hpp"
#include "contracts/machines/machine-service.hpp"
#include "contracts/runtime/runtime-service.hpp"
#include "platform/events/event-sink.hpp"

namespace ember::logic::machines {

/** 机型服务。 */
class MachineManager final : public contracts::machines::MachineService {
 public:
  MachineManager(std::shared_ptr<contracts::machines::RomStorage> storage, std::shared_ptr<contracts::runtime::RuntimeService> runtime, std::shared_ptr<platform::EventSink> events);

  [[nodiscard]] contracts::machines::MachinesListResult list() const override;
  contracts::machines::MachinesSetRomResult setRom(const contracts::machines::MachinesSetRomParams& params) override;
  contracts::machines::MachinesSetRomsResult setRoms(const contracts::machines::MachinesSetRomsParams& params) override;
  contracts::machines::MachinesRemoveRomResult removeRom(const contracts::machines::MachinesRemoveRomParams& params) override;
  contracts::machines::MachinesBootResult boot(const contracts::machines::MachinesBootParams& params) override;

  /** 机型当前设置的 ROM（供运行服务在通电与硬复位时换用新 ROM）；机型不需要 ROM 或还没有设置时为空。@throws shared::CoreError machines.romInvalid。 */
  [[nodiscard]] std::optional<contracts::runtime::MachineRom> currentRom(const std::string& machine) const;

 private:
  /** 清单里的一个机型。 */
  struct Entry {
    std::string id;
    std::string name;
    bool needsRom;
    bool supported;
    /** 需要 ROM 的机型：PRG 的字节数。 */
    std::size_t prgSize;
    /** 可以接在 PRG 后面的可选部分的字节数（中华学习机的汉字字库）；没有时为 0。 */
    std::size_t optionalSize{0};
  };

  [[nodiscard]] const Entry& entry(const std::string& id) const;
  [[nodiscard]] contracts::machines::MachineInfo info(const Entry& item) const;
  /** 检查 ROM 文件的大小符合机型（ROM 是不带文件头的纯 bin）；@throws shared::CoreError machines.romInvalid。 */
  static void checkRom(const Entry& item, const std::vector<std::uint8_t>& file);
  /** 机型已保存的 ROM 的版本：读不出内容时用文件名。 */
  [[nodiscard]] std::string labelOf(const Entry& item, const contracts::machines::StoredRom& stored) const;

  std::shared_ptr<contracts::machines::RomStorage> storage_;
  std::shared_ptr<contracts::runtime::RuntimeService> runtime_;
  std::shared_ptr<platform::EventSink> events_;
  std::vector<Entry> entries_;
};

}  // namespace ember::logic::machines
