/*
 * ------------------------------------------------------------------------
 * 名称：fdc-upd765.cpp
 * 说明：软驱控制器模块的实现。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-15
 * 备注：每次运行一步按固定的主时钟间隔推进，并且空闲时不运行。
 * ------------------------------------------------------------------------
 */

#include "logic/hardware/fdc-upd765/fdc-upd765.hpp"

#include <algorithm>

namespace ember::logic::hardware {

namespace hw = contracts::hardware;

namespace {

constexpr std::uint8_t DOR_SELECT_DRIVE = 0x03;
constexpr std::uint8_t DOR_NOT_RESET = 0x04;
constexpr std::uint8_t DOR_IRQ_ENABLE = 0x08;
constexpr std::uint8_t DOR_MOTOR_FIRST = 0x10;
constexpr std::uint8_t MSR_READY = 0x80;
constexpr std::uint8_t MSR_BUSY = 0x10;
constexpr std::uint8_t MSR_DIRECTION = 0x40;
constexpr std::uint8_t MSR_NO_DMA = 0x20;
constexpr std::uint8_t CMD_MULTI_TRACK = 0x80;
constexpr std::size_t CMD_NUMBER = 0;
constexpr std::size_t CMD_US_HD = 1;
constexpr std::size_t CMD_CYLINDER = 2;
constexpr std::size_t CMD_HEAD = 3;
constexpr std::size_t CMD_SECTOR_NUMBER = 4;
constexpr std::size_t CMD_SECTOR_SIZE = 5;
constexpr std::size_t CMD_LAST_SECTOR = 6;
constexpr std::size_t CMD_FORMAT_SECTOR_SIZE = 2;
constexpr std::size_t CMD_FORMAT_LAST_SECTOR = 3;
constexpr std::size_t CMD_FORMAT_FILL = 5;
constexpr std::size_t CMD_NCN = 2;
constexpr std::uint8_t ST0_NORMAL_TERMINATION = 0x00;
constexpr std::uint8_t ST0_ABNORMAL_TERMINATION = 0x40;
constexpr std::uint8_t ST0_INVALID_COMMAND = 0x80;
constexpr std::uint8_t ST0_POLLING = 0xC0;
constexpr std::uint8_t ST0_SEEK_COMPLETE = 0x20;
constexpr std::uint8_t ST0_EQUIPMENT_CHECK = 0x10;
constexpr std::uint8_t ST1_END_OF_TRACK = 0x80;
constexpr std::uint8_t ST1_DATA_ERROR = 0x20;
constexpr std::uint8_t ST1_NO_DATA = 0x04;
constexpr std::uint8_t ST1_NOT_WRITEABLE = 0x02;
constexpr std::uint8_t ST1_MISSING_ADDRESS_MARK = 0x01;
constexpr std::uint8_t ST2_WRONG_CYLINDER = 0x10;
constexpr std::uint8_t ST2_DATA_ERROR = 0x20;
constexpr std::uint8_t CFG_NOT_POLL = 0x10;
/** 数据率（千位每秒），下标是数据率寄存器的低 2 位。 */
constexpr unsigned DATA_RATES[4] = {500, 300, 250, 1000};
/** 一个扇区的标准大小。 */
constexpr std::size_t STANDARD_SECTOR_SIZE = 512;
/** 扇区大小的上限。 */
constexpr std::size_t MAX_SECTOR_SIZE = 8192;
/** 格式化时“请求扇区编号”占用的字节数（扇区头的 4 个字节）。 */
constexpr std::size_t FORMAT_ID_BYTES = 4;

/** 没有接驱动器：一个不存在的驱动器。 */
class AbsentDrive final : public hw::FloppyDrive {
 public:
  [[nodiscard]] bool exists() const override { return false; }
  [[nodiscard]] bool inserted() const override { return false; }
  [[nodiscard]] bool motorRunning() const override { return false; }
  void setMotor(bool) override {}
  [[nodiscard]] bool writeProtected() const override { return false; }
  [[nodiscard]] bool diskChanged() const override { return false; }
  void clearDiskChanged() override {}
  [[nodiscard]] unsigned cylinder() const override { return 0; }
  void setCylinder(unsigned) override {}
  [[nodiscard]] hw::FloppyFormat format() const override { return {}; }
  [[nodiscard]] std::uint8_t readByte(std::size_t) const override { return 0; }
  void writeByte(std::size_t, std::uint8_t) override {}
  [[nodiscard]] const hw::FloppyFaults& faults() const override { return faults_; }
  void insertImage(std::vector<std::uint8_t>, bool, std::string) override {}
  void ejectImage() override {}
  [[nodiscard]] const std::vector<std::uint8_t>& image() const override { return image_; }
  [[nodiscard]] const std::string& source() const override { return source_; }
  [[nodiscard]] bool modified() const override { return false; }
  [[nodiscard]] hw::FloppyDirtyRange dirtyRange() const override { return {}; }
  void markSaved() override {}
  void applyExternalChange(std::size_t, std::span<const std::uint8_t>) override {}
  void writeFromHost(std::size_t, std::span<const std::uint8_t>) override {}
  [[nodiscard]] std::uint64_t hostChangeCount() const override { return 0; }
  void setModifiedListener(std::function<void()>) override {}
  void setWriteProtected(bool) override {}
  hw::FloppyFaults& mutableFaults() override { return faults_; }
  bool suspendMedia() override { return false; }
  bool restoreMedia() override { return false; }
  [[nodiscard]] bool mediaSuspended() const override { return false; }

 private:
  hw::FloppyFaults faults_;
  std::vector<std::uint8_t> image_;
  std::string source_;
};

AbsentDrive& absentDrive() {
  static AbsentDrive instance;
  return instance;
}

}  // namespace

const std::array<FdcUpd765::CommandInfo, 0x20> FdcUpd765::COMMANDS = [] {
  std::array<CommandInfo, 0x20> table;
  table.fill({1, &FdcUpd765::cmInvalid});
  table[0x02] = {9, &FdcUpd765::cmInvalid};
  table[0x03] = {3, &FdcUpd765::cmSpecify};
  table[0x04] = {2, &FdcUpd765::cmSenseDriveStatus};
  table[0x05] = {9, &FdcUpd765::cmWriteData};
  table[0x06] = {9, &FdcUpd765::cmReadData};
  table[0x07] = {2, &FdcUpd765::cmRecalibrate};
  table[0x08] = {1, &FdcUpd765::cmSenseInterruptStatus};
  table[0x09] = {9, &FdcUpd765::cmInvalid};
  table[0x0A] = {2, &FdcUpd765::cmReadId};
  table[0x0C] = {9, &FdcUpd765::cmInvalid};
  table[0x0D] = {6, &FdcUpd765::cmFormatTrack};
  table[0x0F] = {3, &FdcUpd765::cmSeek};
  table[0x11] = {9, &FdcUpd765::cmInvalid};
  table[0x12] = {2, &FdcUpd765::cmInvalid};
  table[0x13] = {4, &FdcUpd765::cmConfigure};
  return table;
}();

FdcUpd765::FdcUpd765(hw::Scheduler& scheduler, hw::InterruptSink& sink, std::uint8_t interruptSource, hw::MasterTime stepTicks, unsigned pollSteps)
    : scheduler_(scheduler), sink_(sink), interruptSource_(interruptSource), stepTicks_(stepTicks), pollSteps_(pollSteps) {
  reset(true);
}

void FdcUpd765::attachDrive(unsigned index, hw::FloppyDrive* drive) {
  if (index < DRIVE_COUNT) drives_[index] = drive;
}

hw::FloppyDrive& FdcUpd765::driveAt(unsigned index) const { return drives_[index] != nullptr ? *drives_[index] : absentDrive(); }

hw::FloppyDrive& FdcUpd765::drive() { return driveAt(dor_ & DOR_SELECT_DRIVE); }

void FdcUpd765::reset(bool power) {
  if (eventId_ != hw::NO_EVENT) scheduler_.cancel(eventId_);
  eventId_ = hw::NO_EVENT;
  if (power) {
    useDma_ = false;
    config_ = 0;
    dataRate_ = DATA_RATES[0];
    resetPin_ = false;
    cylinder_ = head_ = sectorNumber_ = 0;
    sectorSize_ = 0;
  }
  writeRegister(2, 0);
  resetController();
  irqLevel_ = false;
}

void FdcUpd765::resetController() {
  phase_ = &FdcUpd765::commandPhase;
  msr_ = MSR_READY;
  sra_ = st0_ = st1_ = st2_ = 0;
  dataPosition_.reset();
  bytesLeft_ = 0;
  command_.clear();
  resultQueue_.clear();
  dmaRequest_ = dmaTerminate_ = false;
  pollTimer_ = 0;
  pollDrive_ = 0;
  pollDrivesLeft_ = DRIVE_COUNT;
  previouslyReady_.fill(false);
}

// ---------------------------------------------------------------- 调度

bool FdcUpd765::unresponsive() const {
  return std::any_of(drives_.begin(), drives_.end(), [](const hw::FloppyDrive* unit) { return unit != nullptr && unit->faults().unresponsive; });
}

bool FdcUpd765::idle() const {
  if ((dor_ & DOR_NOT_RESET) == 0 || resetPin_ || unresponsive()) return true;
  return phase_ == &FdcUpd765::commandPhase && (msr_ & MSR_READY) != 0 && (pollDrivesLeft_ == 0 || (config_ & CFG_NOT_POLL) != 0);
}

void FdcUpd765::arm() {
  if (eventId_ != hw::NO_EVENT || idle()) return;
  eventId_ = scheduler_.scheduleAt(scheduler_.now() + stepTicks_, *this, 0);
}

void FdcUpd765::onEvent(hw::MasterTime /*when*/, std::uint32_t /*tag*/) {
  eventId_ = hw::NO_EVENT;
  step();
  checkInterrupt();
  arm();
}

void FdcUpd765::step() {
  if ((dor_ & DOR_NOT_RESET) != 0 && !resetPin_ && !unresponsive()) (this->*phase_)();
}

void FdcUpd765::checkInterrupt() {
  const bool level = (sra_ & SRA_IRQ) != 0 && (dor_ & DOR_IRQ_ENABLE) != 0;
  if (level && !irqLevel_) sink_.raise(interruptSource_);
  irqLevel_ = level;
}

// ---------------------------------------------------------------- 阶段

void FdcUpd765::commandPhase() {
  pollDrives();
  if ((msr_ & MSR_READY) != 0) return;
  // 数据字节已经写进锁存器。
  msr_ |= MSR_BUSY;
  command_.push_back(latch_);
  const CommandInfo& info = COMMANDS[command_[CMD_NUMBER] & 0x1F];
  // 这条命令的字节收齐了就开始执行阶段，否则等下一个字节。
  if (command_.size() == info.length) {
    phase_ = info.handler;
    dataPosition_.reset();
    bytesLeft_ = 0;
    if ((command_[CMD_NUMBER] & 0x1F) != 0x08) {
      st0_ = command_.size() > CMD_US_HD ? static_cast<std::uint8_t>(command_[CMD_US_HD] & 7) : std::uint8_t{0};
      st1_ = st2_ = 0;
    }
  } else {
    msr_ |= MSR_READY;
  }
}

void FdcUpd765::resultPhase() {
  if ((msr_ & MSR_READY) != 0) return;
  // 结果字节都被读走了就回到命令阶段，否则给出下一个字节。
  if (resultQueue_.empty()) {
    msr_ &= static_cast<std::uint8_t>(~MSR_DIRECTION);
    msr_ &= static_cast<std::uint8_t>(~MSR_BUSY);
    command_.clear();
    phase_ = &FdcUpd765::commandPhase;
    dmaRequest_ = false;
  } else {
    msr_ |= MSR_DIRECTION;
    latch_ = resultQueue_.front();
    resultQueue_.pop_front();
  }
  msr_ |= MSR_READY;
}

void FdcUpd765::endCommand(bool raiseIrq, std::initializer_list<std::uint8_t> results) {
  for (const std::uint8_t value : results) resultQueue_.push_back(value);
  phase_ = &FdcUpd765::resultPhase;
  if (raiseIrq) sra_ |= SRA_IRQ;
  msr_ &= static_cast<std::uint8_t>(~MSR_NO_DMA);
}

std::uint8_t FdcUpd765::sectorSizeCode() const { return command_.size() > CMD_SECTOR_SIZE ? command_[CMD_SECTOR_SIZE] : std::uint8_t{2}; }

void FdcUpd765::nextSector() {
  hw::FloppyDrive& unit = drive();
  const bool firstSector = !dataPosition_.has_value();
  if (firstSector) {
    cylinder_ = command_[CMD_CYLINDER];
    head_ = command_[CMD_HEAD];
    sectorNumber_ = command_[CMD_SECTOR_NUMBER];
    sectorSize_ = static_cast<std::size_t>(128) << command_[CMD_SECTOR_SIZE];
    if (sectorSize_ > MAX_SECTOR_SIZE) sectorSize_ = MAX_SECTOR_SIZE;
  } else if (sectorNumber_ == command_[CMD_LAST_SECTOR]) {
    if ((command_[CMD_NUMBER] & CMD_MULTI_TRACK) != 0 && head_ == 0) {
      head_ ^= 1;
      command_[CMD_HEAD] ^= 1;
      sectorNumber_ = 1;
    } else if (useDma_ && dmaTerminate_) {
      ++cylinder_;
      head_ ^= 1;
      sectorNumber_ = 1;
    } else {
      st0_ |= ST0_ABNORMAL_TERMINATION;
      st1_ |= ST1_END_OF_TRACK;
      endCommand(true, {st0_, st1_, st2_, cylinder_, head_, sectorNumber_, sectorSizeCode()});
      return;
    }
  } else {
    ++sectorNumber_;
  }

  if (useDma_ && dmaTerminate_) {
    st0_ |= ST0_NORMAL_TERMINATION;
    endCommand(true, {st0_, st1_, st2_, cylinder_, head_, sectorNumber_, sectorSizeCode()});
    dmaTerminate_ = false;
    return;
  }

  const hw::FloppyFormat format = unit.format();
  if (!unit.inserted() || cylinder_ >= format.cylinders || head_ >= format.sides || !unit.motorRunning() || dataRate_ != format.dataRate) {
    st1_ |= ST1_MISSING_ADDRESS_MARK;
    st0_ |= ST0_ABNORMAL_TERMINATION;
  } else if (cylinder_ != unit.cylinder()) {
    st1_ |= ST1_NO_DATA;
    st2_ |= ST2_WRONG_CYLINDER;
    st0_ |= ST0_ABNORMAL_TERMINATION;
  } else if (sectorNumber_ < 1 || sectorNumber_ > format.sectors || sectorSize_ != STANDARD_SECTOR_SIZE) {
    st1_ |= ST1_NO_DATA;
    st0_ |= ST0_ABNORMAL_TERMINATION;
  }

  if ((st0_ & ST0_ABNORMAL_TERMINATION) != 0) {
    endCommand(true, {st0_, st1_, st2_, cylinder_, head_, sectorNumber_, sectorSizeCode()});
    return;
  }

  const int lba = static_cast<int>((cylinder_ * format.sides + head_) * format.sectors + sectorNumber_ - 1);
  // 注入的坏扇区：只有读才出数据校验错误。
  const std::vector<int>& bad = unit.faults().badSectors;
  if ((command_[CMD_NUMBER] & 0x1F) == 0x06 && std::find(bad.begin(), bad.end(), lba) != bad.end()) {
    st0_ |= ST0_ABNORMAL_TERMINATION;
    st1_ |= ST1_DATA_ERROR;
    st2_ |= ST2_DATA_ERROR;
    endCommand(true, {st0_, st1_, st2_, cylinder_, head_, sectorNumber_, sectorSizeCode()});
    return;
  }
  bytesLeft_ = sectorSize_;
  dataPosition_ = static_cast<std::size_t>(lba) * sectorSize_;
}

void FdcUpd765::pollDrives() {
  if ((config_ & CFG_NOT_POLL) != 0 || pollDrivesLeft_ == 0) return;
  if (pollTimer_ != 0) {
    --pollTimer_;
    return;
  }
  pollTimer_ = pollSteps_;
  const bool ready = driveAt(pollDrive_).exists();
  if (previouslyReady_[pollDrive_] != ready) {
    previouslyReady_[pollDrive_] = ready;
    st0_ = static_cast<std::uint8_t>((st0_ & ~ST0_SEEK_COMPLETE) | ST0_POLLING | pollDrive_);
    sra_ |= SRA_IRQ;
  }
  pollDrive_ = (pollDrive_ + 1) & 3;
  --pollDrivesLeft_;
}

// ---------------------------------------------------------------- 命令

void FdcUpd765::cmReadId() {
  hw::FloppyDrive& unit = drive();
  if (unit.faults().noIndex) return;
  const hw::FloppyFormat format = unit.format();
  if (!unit.inserted() || cylinder_ >= format.cylinders || head_ >= format.sides || !unit.motorRunning() || dataRate_ != format.dataRate) {
    st1_ |= ST1_MISSING_ADDRESS_MARK;
    st0_ |= ST0_ABNORMAL_TERMINATION;
  } else {
    cylinder_ = static_cast<std::uint8_t>(unit.cylinder());
    head_ = static_cast<std::uint8_t>(command_[CMD_US_HD] >> 2);
    sectorSize_ = STANDARD_SECTOR_SIZE;
    if (sectorNumber_ == 0 || sectorNumber_ >= format.sectors) sectorNumber_ = 1;
    st0_ |= ST0_NORMAL_TERMINATION;
  }
  endCommand(true, {st0_, st1_, st2_, cylinder_, head_, sectorNumber_, sectorSizeCode()});
}

void FdcUpd765::cmReadData() {
  hw::FloppyDrive& unit = drive();
  if (unit.faults().noIndex) return;
  // 等着 CPU 取走字节时什么也不做（不模拟溢出）。
  if ((msr_ & MSR_READY) != 0 || dmaRequest_) return;
  if (bytesLeft_ == 0) nextSector();
  if (bytesLeft_ == 0) return;
  latch_ = unit.readByte((*dataPosition_)++);
  --bytesLeft_;
  if (useDma_) {
    dmaRequest_ = true;
  } else {
    msr_ |= MSR_READY | MSR_NO_DMA | MSR_DIRECTION;
    sra_ |= SRA_IRQ;
  }
}

void FdcUpd765::cmWriteData() {
  hw::FloppyDrive& unit = drive();
  if (unit.faults().noIndex) return;
  if (unit.writeProtected()) {
    st0_ |= ST0_ABNORMAL_TERMINATION;
    st1_ |= ST1_NOT_WRITEABLE;
    endCommand(true, {st0_, st1_, st2_, cylinder_, head_, sectorNumber_, command_[CMD_FORMAT_SECTOR_SIZE]});
    return;
  }
  // 等着 CPU 给字节时什么也不做（不模拟溢出）。
  if ((msr_ & MSR_READY) != 0 || dmaRequest_) return;
  if (bytesLeft_ == 0) {
    nextSector();
  } else {
    unit.writeByte((*dataPosition_)++, latch_);
    --bytesLeft_;
  }
  if (bytesLeft_ != 0) {
    if (useDma_) {
      dmaRequest_ = true;
    } else {
      msr_ |= MSR_READY | MSR_NO_DMA;
      msr_ &= static_cast<std::uint8_t>(~MSR_DIRECTION);
      sra_ |= SRA_IRQ;
    }
  }
}

void FdcUpd765::cmFormatTrack() {
  hw::FloppyDrive& unit = drive();
  if (unit.faults().noIndex) return;
  if (unit.writeProtected()) {
    st0_ |= ST0_ABNORMAL_TERMINATION;
    st1_ |= ST1_NOT_WRITEABLE;
    endCommand(true, {st0_, st1_, st2_, cylinder_, head_, sectorNumber_, command_[CMD_FORMAT_SECTOR_SIZE]});
    return;
  }
  // 等着 CPU 给扇区编号时什么也不做（不模拟溢出）。
  if ((msr_ & MSR_READY) != 0 || dmaRequest_) return;
  const hw::FloppyFormat format = unit.format();
  if (bytesLeft_ == 0) {
    if (!dataPosition_.has_value()) {
      cylinder_ = static_cast<std::uint8_t>(unit.cylinder());
      head_ = static_cast<std::uint8_t>(command_[CMD_US_HD] >> 2);
      sectorNumber_ = 1;
      sectorSize_ = std::min(static_cast<std::size_t>(128) << command_[CMD_FORMAT_SECTOR_SIZE], MAX_SECTOR_SIZE);
    } else if (++sectorNumber_ > command_[CMD_FORMAT_LAST_SECTOR]) {
      st0_ |= ST0_NORMAL_TERMINATION;
      // 格式化最后一个编号字节时程序会给出终止计数，不能留给下一条命令。
      dmaTerminate_ = false;
      endCommand(true, {st0_, st1_, st2_, cylinder_, head_, sectorNumber_, command_[CMD_FORMAT_SECTOR_SIZE]});
      return;
    }
    const int lba = static_cast<int>((cylinder_ * format.sides + head_) * format.sectors + sectorNumber_ - 1);
    bytesLeft_ = sectorSize_;
    dataPosition_ = static_cast<std::size_t>(lba) * sectorSize_;
  } else {
    unit.writeByte((*dataPosition_)++, command_[CMD_FORMAT_FILL]);
    --bytesLeft_;
  }
  // 填充扇区头的 4 个字节时请求扇区编号。
  if (bytesLeft_ > STANDARD_SECTOR_SIZE - FORMAT_ID_BYTES) {
    if (useDma_) {
      dmaRequest_ = true;
    } else {
      msr_ |= MSR_READY | MSR_NO_DMA;
      msr_ &= static_cast<std::uint8_t>(~MSR_DIRECTION);
      sra_ |= SRA_IRQ;
    }
  }
}

void FdcUpd765::cmSeek() {
  hw::FloppyDrive& unit = drive();
  if (!unit.faults().seekFail) {
    if (unit.cylinder() != command_[CMD_NCN]) unit.clearDiskChanged();
    unit.setCylinder(command_[CMD_NCN]);
  }
  st0_ |= ST0_SEEK_COMPLETE;
  if (!unit.exists()) st0_ |= ST0_ABNORMAL_TERMINATION;
  endCommand(true, {});
}

void FdcUpd765::cmRecalibrate() {
  hw::FloppyDrive& unit = drive();
  if (unit.faults().seekFail && unit.cylinder() != 0) {
    // 退不回 0 磁道：寻道结束并报设备检查错误。
    st0_ |= ST0_SEEK_COMPLETE | ST0_EQUIPMENT_CHECK | ST0_ABNORMAL_TERMINATION;
    endCommand(true, {});
    return;
  }
  if (unit.cylinder() != 0) unit.clearDiskChanged();
  unit.setCylinder(0);
  st0_ |= ST0_SEEK_COMPLETE;
  if (!unit.exists()) st0_ |= ST0_ABNORMAL_TERMINATION;
  endCommand(true, {});
}

void FdcUpd765::cmSenseInterruptStatus() {
  sra_ &= static_cast<std::uint8_t>(~SRA_IRQ);
  endCommand(false, {st0_, static_cast<std::uint8_t>(drive().cylinder())});
}

void FdcUpd765::cmSpecify() {
  useDma_ = (command_[2] & 1) == 0;
  endCommand(false, {});
}

void FdcUpd765::cmSenseDriveStatus() {
  hw::FloppyDrive& unit = drive();
  endCommand(false, {static_cast<std::uint8_t>((dor_ & DOR_SELECT_DRIVE) | (head_ << 2) | (unit.cylinder() == 0 ? 0x10 : 0x00) | (unit.writeProtected() ? 0x40 : 0x00) | (unit.inserted() ? 0x20 : 0x00) | 0x28)});
}

void FdcUpd765::cmConfigure() {
  config_ = command_[2];
  endCommand(false, {});
}

void FdcUpd765::cmInvalid() {
  st0_ |= ST0_INVALID_COMMAND;
  endCommand(false, {st0_});
}

// ---------------------------------------------------------------- 端口

std::uint8_t FdcUpd765::readRegister(unsigned port) {
  std::uint8_t result = 0xFF;
  switch (port & 7) {
    case 0:
      result = sra_;
      break;
    case 2:
      result = dor_;
      break;
    case 3:
      result = (sra_ & SRA_IRQ) != 0 ? 0x40 : 0x00;
      break;
    case 4:
      result = msr_;
      break;
    case 5:
      if ((msr_ & MSR_READY) != 0 && (msr_ & MSR_DIRECTION) != 0) {
        msr_ &= static_cast<std::uint8_t>(~MSR_READY);
        sra_ &= static_cast<std::uint8_t>(~SRA_IRQ);
        result = latch_;
      }
      break;
    case 7: {
      hw::FloppyDrive& unit = drive();
      result = unit.diskChanged() ? 0x80 : 0x00;
      unit.clearDiskChanged();
      break;
    }
    default:
      break;
  }
  checkInterrupt();
  arm();
  return result;
}

std::uint8_t FdcUpd765::peekRegister(unsigned port) const {
  switch (port & 7) {
    case 0:
      return sra_;
    case 2:
      return dor_;
    case 4:
      return msr_;
    case 5:
      return (msr_ & MSR_READY) != 0 && (msr_ & MSR_DIRECTION) != 0 ? latch_ : std::uint8_t{0xFF};
    default:
      return 0xFF;
  }
}

void FdcUpd765::writeRegister(unsigned port, std::uint8_t value) {
  switch (port & 7) {
    case 2:
      if ((dor_ & DOR_NOT_RESET) != 0 && (value & DOR_NOT_RESET) == 0) resetController();
      dor_ = value;
      for (unsigned index = 0; index < DRIVE_COUNT; ++index) {
        if (drives_[index] != nullptr) drives_[index]->setMotor((dor_ & (DOR_MOTOR_FIRST << index)) != 0 && drives_[index]->exists());
      }
      break;
    case 3:
      sra_ &= static_cast<std::uint8_t>(~SRA_IRQ);
      break;
    case 5:
      if ((msr_ & MSR_READY) != 0 && (msr_ & MSR_DIRECTION) == 0) {
        msr_ &= static_cast<std::uint8_t>(~MSR_READY);
        sra_ &= static_cast<std::uint8_t>(~SRA_IRQ);
        latch_ = value;
      }
      break;
    case 7:
      dataRate_ = DATA_RATES[value & 3];
      break;
    default:
      break;
  }
  checkInterrupt();
  arm();
}

std::uint8_t FdcUpd765::receiveFromDma() {
  dmaRequest_ = false;
  arm();
  return latch_;
}

void FdcUpd765::sendToDma(std::uint8_t value) {
  latch_ = value;
  dmaRequest_ = false;
  arm();
}

void FdcUpd765::setTerminalCount() { dmaTerminate_ = true; }

void FdcUpd765::setResetPin(bool high) {
  if (!resetPin_ && high) resetController();
  resetPin_ = high;
  arm();
}

}  // namespace ember::logic::hardware
