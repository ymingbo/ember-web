/*
 * ------------------------------------------------------------------------
 * 名称：standard-floppy-drive.cpp
 * 说明：软驱驱动器模块的实现。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-15
 * ------------------------------------------------------------------------
 */

#include "logic/hardware/floppy-drive/standard-floppy-drive.hpp"

#include <algorithm>
#include <utility>

namespace ember::logic::hardware {

namespace hw = contracts::hardware;

namespace {

constexpr std::size_t KILOBYTE = 1024;

}  // namespace

hw::FloppyFormat StandardFloppyDrive::formatFor(std::size_t imageSize) {
  switch (imageSize / KILOBYTE) {
    case 160:
      return {40, 1, 8, 250};
    case 180:
      return {40, 1, 9, 250};
    case 320:
      return {40, 2, 8, 250};
    case 360:
      return {40, 2, 9, 250};
    case 720:
      return {80, 2, 9, 250};
    case 1200:
      return {80, 2, 15, 500};
    case 1440:
      return {80, 2, 18, 500};
    case 2880:
      return {80, 2, 36, 1000};
    default:
      // 非标准大小暂按 1.44MB 处理。
      return {80, 2, 18, 500};
  }
}

void StandardFloppyDrive::writeByte(std::size_t offset, std::uint8_t value) {
  if (!inserted_ || offset >= image_.size()) return;
  image_[offset] = value;
  const bool wasClean = dirty_.empty();
  if (wasClean) {
    dirty_ = {offset, offset + 1};
  } else {
    dirty_.begin = std::min(dirty_.begin, offset);
    dirty_.end = std::max(dirty_.end, offset + 1);
  }
  if (wasClean && onModified_) onModified_();
}

void StandardFloppyDrive::applyExternalChange(std::size_t offset, std::span<const std::uint8_t> bytes) {
  if ((!inserted_ && !suspended_) || offset > image_.size() || bytes.size() > image_.size() - offset) return;
  std::copy(bytes.begin(), bytes.end(), image_.begin() + static_cast<std::ptrdiff_t>(offset));
  ++hostChanges_;
}

void StandardFloppyDrive::writeFromHost(std::size_t offset, std::span<const std::uint8_t> bytes) {
  if (!inserted_ || bytes.empty() || offset > image_.size() || bytes.size() > image_.size() - offset) return;
  std::copy(bytes.begin(), bytes.end(), image_.begin() + static_cast<std::ptrdiff_t>(offset));
  ++hostChanges_;
  changed_ = true;
  const std::size_t end = offset + bytes.size();
  const bool wasClean = dirty_.empty();
  if (wasClean) {
    dirty_ = {offset, end};
  } else {
    dirty_.begin = std::min(dirty_.begin, offset);
    dirty_.end = std::max(dirty_.end, end);
  }
  if (wasClean && onModified_) onModified_();
}

void StandardFloppyDrive::insertImage(std::vector<std::uint8_t> image, bool writeProtected, std::string source) {
  image_ = std::move(image);
  source_ = std::move(source);
  format_ = formatFor(image_.size());
  writeProtected_ = writeProtected;
  inserted_ = true;
  suspended_ = false;
  dirty_ = {};
}

void StandardFloppyDrive::ejectImage() {
  if (inserted_ || suspended_) changed_ = true;
  inserted_ = false;
  suspended_ = false;
  dirty_ = {};
  image_.clear();
  image_.shrink_to_fit();
  source_.clear();
}

bool StandardFloppyDrive::suspendMedia() {
  if (!inserted_) return false;
  inserted_ = false;
  suspended_ = true;
  changed_ = true;
  return true;
}

void StandardFloppyDrive::attach(const void* owner, std::function<void(bool)> onMotor) {
  // 先让马达停转（通知还是原来的），再换成新主机的。
  setMotor(false);
  cylinder_ = 0;
  owner_ = owner;
  onMotorChanged = std::move(onMotor);
}

void StandardFloppyDrive::detach(const void* owner) {
  if (owner_ != owner) return;
  setMotor(false);
  owner_ = nullptr;
  onMotorChanged = nullptr;
}

bool StandardFloppyDrive::restoreMedia() {
  if (!suspended_) return false;
  inserted_ = true;
  suspended_ = false;
  return true;
}

}  // namespace ember::logic::hardware
