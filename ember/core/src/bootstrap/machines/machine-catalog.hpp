/*
 * ------------------------------------------------------------------------
 * 名称：machine-catalog.hpp
 * 说明：主机型号目录：按型号名创建主机；有 fc（经典 FC，要卡带）、sb97（小霸王 97 学习机，要 ROM）、sb2000（小霸王 2000 学习机，要 ROM）、bbg1（步步高软驱一号，要 ROM）、bbg98（步步高 98，要 ROM）、pec9588（金字塔 PEC-9588，要 ROM）、pec586（金字塔 PEC-586(686)，要 ROM）、yuxing（裕兴学习机，要 ROM）与 cec1（中华学习机 CEC-I，要 ROM）。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * 备注：增加主机型号时在这里登记，同时在 mappers 模块的主机型号目录里登记（bootstrap/modules/mappers.cpp）。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <memory>

#include "contracts/hardware/floppy-activity.hpp"
#include "contracts/hardware/printer-output.hpp"
#include "contracts/runtime/machine-factory.hpp"
#include "logic/hardware/apu-2a03/apu-2a03.hpp"
#include "logic/hardware/floppy-drive/standard-floppy-drive.hpp"
#include "logic/hardware/tape-deck/tape-deck.hpp"

namespace ember::bootstrap {

/** 程序里已有的主机型号。 */
class MachineCatalog final : public contracts::runtime::MachineFactory {
 public:
  /** 全部主机型号输出声音的采样率（赫兹）；声音输出按它打开设备。 */
  static constexpr unsigned AUDIO_SAMPLE_RATE = logic::hardware::Apu2a03::SAMPLE_RATE;

  /**
   * @param printerOutput 整机机型的打印机把输出的页交给它；为空时主机不接打印机。
   */
  explicit MachineCatalog(std::shared_ptr<contracts::hardware::PrinterOutput> printerOutput = nullptr, std::shared_ptr<contracts::hardware::FloppyActivity> floppyActivity = nullptr)
      : printerOutput_(std::move(printerOutput)), floppyActivity_(std::move(floppyActivity)) {}

  /** 磁带机：独立于主机的设备，目录里所有有磁带接口的主机都接到这一台；介质服务用同一台载入与播放磁带。 */
  [[nodiscard]] std::shared_ptr<contracts::hardware::TapeControl> tapeControl() const { return tapeDeck_; }

  /** 软驱 A：独立于主机的设备，盘片随时可以插入与取出（没有主机、没有通电也行）；学习机创建时接入它，盘片留在里面。 */
  [[nodiscard]] std::shared_ptr<contracts::hardware::FloppyDrive> floppyDrive() const { return floppyDrive_; }

  [[nodiscard]] std::unique_ptr<contracts::hardware::Machine> create(const std::string& host) const override;
  [[nodiscard]] std::unique_ptr<contracts::hardware::Machine> createWithRom(const std::string& machine, std::vector<std::uint8_t> rom) const override;

 private:
  std::shared_ptr<contracts::hardware::PrinterOutput> printerOutput_;
  std::shared_ptr<contracts::hardware::FloppyActivity> floppyActivity_;
  std::shared_ptr<logic::hardware::TapeDeck> tapeDeck_ = std::make_shared<logic::hardware::TapeDeck>();
  std::shared_ptr<logic::hardware::StandardFloppyDrive> floppyDrive_ = std::make_shared<logic::hardware::StandardFloppyDrive>();
};

}  // namespace ember::bootstrap
