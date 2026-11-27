/*
 * ------------------------------------------------------------------------
 * 名称：disk2.cpp
 * 说明：5.25 英寸软驱控制器模块的实现。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-25
 * 备注：无
 * ------------------------------------------------------------------------
 */

#include "logic/hardware/disk2/disk2.hpp"

#include <algorithm>
#include <span>

namespace ember::logic::hardware {

namespace hw = contracts::hardware;

namespace {

constexpr unsigned REGISTER_MOTOR_OFF = 8;
constexpr unsigned REGISTER_MOTOR_ON = 9;
constexpr unsigned REGISTER_DRIVE_1 = 10;
constexpr unsigned REGISTER_DRIVE_2 = 11;
constexpr unsigned REGISTER_Q6_LOW = 12;
constexpr unsigned REGISTER_Q6_HIGH = 13;
constexpr unsigned REGISTER_Q7_LOW = 14;
constexpr unsigned REGISTER_Q7_HIGH = 15;
constexpr std::uint8_t WRITE_PROTECT_BIT = 0x80;
/** 一个数据寄存器移出的位数。 */
constexpr unsigned BITS_PER_BYTE = 8;
/** 没有盘片时读头读到的杂波，一圈的位数。 */
constexpr std::size_t NOISE_TRACK_BITS = 50000;

}  // namespace

Disk2::Disk2(hw::Scheduler& scheduler, double ticksPerBit, hw::MasterTime motorOffDelay, disk2::FormatChooser chooser)
    : scheduler_(scheduler), ticksPerBit_(ticksPerBit), motorOffDelay_(motorOffDelay), chooser_(chooser) {}

void Disk2::attachDrive(hw::FloppyDrive* drive) {
  drive_ = drive;
  imageData_ = nullptr;
  imageSize_ = 0;
  imageInserted_ = false;
  format_.reset();
  dropCaches();
}

void Disk2::reset(bool power) {
  if (motorEvent_ != hw::NO_EVENT) scheduler_.cancel(motorEvent_);
  motorEvent_ = hw::NO_EVENT;
  magnets_ = 0;
  motorOn_ = false;
  selected_ = 0;
  q6_ = false;
  q7_ = false;
  latch_ = 0;
  cursor_ = 0;
  dropCaches();
  if (power) halfTrack_ = 0;
  updateMotor();
  if (drive_ != nullptr) drive_->setCylinder(track());
}

std::optional<std::uint8_t> Disk2::read(unsigned registerNumber) { return access(registerNumber & 0x0FU, false, 0); }

void Disk2::write(unsigned registerNumber, std::uint8_t value) { access(registerNumber & 0x0FU, true, value); }

std::optional<std::uint8_t> Disk2::access(unsigned registerNumber, bool isWrite, std::uint8_t value) {
  refreshMedia();
  advance();
  switch (registerNumber) {
    case REGISTER_MOTOR_OFF:
      // 停转要晚一段时间：这段时间里再开马达，盘片还在转。
      if (motorOn_ && motorEvent_ == hw::NO_EVENT) motorEvent_ = scheduler_.scheduleAt(scheduler_.now() + motorOffDelay_, *this, 0);
      break;
    case REGISTER_MOTOR_ON:
      if (motorEvent_ != hw::NO_EVENT) scheduler_.cancel(motorEvent_);
      motorEvent_ = hw::NO_EVENT;
      motorOn_ = true;
      updateMotor();
      break;
    case REGISTER_DRIVE_1:
    case REGISTER_DRIVE_2:
      selected_ = registerNumber == REGISTER_DRIVE_1 ? 0 : 1;
      updateMotor();
      break;
    case REGISTER_Q6_LOW:
      q6_ = false;
      // 写模式下切回移位态：数据寄存器的 8 位从下一个位开始移出。
      if (q7_) {
        shiftReg_ = latch_;
        shiftLeft_ = BITS_PER_BYTE;
      }
      break;
    case REGISTER_Q6_HIGH:
      q6_ = true;
      if (q7_ && isWrite) latch_ = value;
      break;
    case REGISTER_Q7_LOW:
      if (q7_) finishWrite();
      q7_ = false;
      readShift_ = 0;
      readReady_ = false;
      break;
    case REGISTER_Q7_HIGH:
      if (!q7_) {
        q7_ = true;
        shiftLeft_ = 0;
        sessionDirty_ = false;
      }
      if (q6_ && isWrite) latch_ = value;
      break;
    default:
      setPhase(registerNumber >> 1, (registerNumber & 1U) != 0);
      break;
  }
  if ((registerNumber & 1U) != 0) return std::nullopt;
  if (q7_) return latch_;
  if (q6_) return static_cast<std::uint8_t>(drive_ != nullptr && drive_->exists() && drive_->inserted() && drive_->writeProtected() ? WRITE_PROTECT_BIT : 0);
  return readData();
}

void Disk2::setPhase(unsigned phase, bool on) {
  const auto bit = static_cast<std::uint8_t>(1U << phase);
  magnets_ = static_cast<std::uint8_t>(on ? (magnets_ | bit) : (magnets_ & ~bit));
  if (selected_ != 0) return;
  // 读头所在位置对着的磁铁不动；相邻的磁铁吸引读头往那个方向走半个磁道，两边都吸时不动。
  int direction = 0;
  if ((magnets_ & (1U << ((halfTrack_ + 1) & 3U))) != 0) ++direction;
  if ((magnets_ & (1U << ((halfTrack_ + 3) & 3U))) != 0) --direction;
  if (direction == 0) return;
  const int next = std::clamp(static_cast<int>(halfTrack_) + direction, 0, static_cast<int>(MAX_HALF_TRACK));
  if (static_cast<unsigned>(next) == halfTrack_) return;
  halfTrack_ = static_cast<unsigned>(next);
  readShift_ = 0;
  readReady_ = false;
  if (drive_ != nullptr) drive_->setCylinder(track());
}

void Disk2::updateMotor() {
  if (drive_ != nullptr) drive_->setMotor(motorOn_ && selected_ == 0);
}

void Disk2::refreshMedia() {
  if (drive_ == nullptr) return;
  const std::vector<std::uint8_t>& image = drive_->image();
  const bool inserted = drive_->inserted();
  const std::uint64_t hostChanges = drive_->hostChangeCount();
  const bool sameMedia = image.data() == imageData_ && image.size() == imageSize_ && inserted == imageInserted_;
  // 主机一侧改了镜像：学习机正在写盘时先不处理（丢掉缓存会丢掉写到一半的磁道），写完后的下一次访问再处理
  if (sameMedia && (hostChanges == hostChanges_ || q7_)) return;
  hostChanges_ = hostChanges;
  imageData_ = image.data();
  imageSize_ = image.size();
  imageInserted_ = inserted;
  format_ = inserted ? chooser_(image) : std::nullopt;
  woz_.reset();
  if (format_.has_value() && format_->layout == disk2::Layout::woz) {
    woz_ = disk2::parseWoz(image);
    if (!woz_.has_value()) format_.reset();
  }
  dropCaches();
}

void Disk2::dropCaches() {
  for (auto& ring : rings_) ring.clear();
  readShift_ = 0;
  readReady_ = false;
  shiftLeft_ = 0;
  sessionDirty_ = false;
}

bool Disk2::spinning() const { return motorOn_ && selected_ == 0 && drive_ != nullptr && drive_->exists(); }

bool Disk2::hasDisk() const { return drive_ != nullptr && drive_->inserted() && format_.has_value(); }

unsigned Disk2::ringIndex() const { return format_.has_value() && format_->layout == disk2::Layout::woz ? halfTrack_ : track(); }

const disk2::WozTrack* Disk2::wozTrackAt(unsigned halfTrack) const {
  if (!woz_.has_value()) return nullptr;
  const std::int16_t index = woz_->trackMap[std::min<std::size_t>(halfTrack * 2U, woz_->trackMap.size() - 1)];
  return index < 0 ? nullptr : &woz_->tracks[static_cast<std::size_t>(index)];
}

std::vector<std::uint8_t>& Disk2::trackBits(unsigned index) {
  std::vector<std::uint8_t>& ring = rings_[std::min<unsigned>(index, static_cast<unsigned>(rings_.size() - 1))];
  if (!ring.empty()) return ring;
  const auto makeNoise = [&ring, index] {
    // 读头读到杂波：每个磁道一圈固定的随机位。
    std::uint32_t state = 0x9E3779B9U * (index + 1U);
    ring.resize(NOISE_TRACK_BITS);
    for (auto& bit : ring) {
      state ^= state << 13;
      state ^= state >> 17;
      state ^= state << 5;
      bit = static_cast<std::uint8_t>(state & 1U);
    }
  };
  if (!hasDisk()) {
    makeNoise();
    return ring;
  }
  const std::span<const std::uint8_t> image(drive_->image());
  switch (format_->layout) {
    case disk2::Layout::sector:
      if (index < format_->trackCount) {
        ring = disk2::encodeTrack(*format_, index, image.subspan(index * format_->trackBytes(), format_->trackBytes()));
      } else {
        // 镜像之外的磁道（格式化会写到这里）：什么都没有。
        ring.assign(format_->trackBits, 0);
      }
      break;
    case disk2::Layout::nibble:
      if (index < format_->trackCount) ring = disk2::nibblesToBits(image.subspan(index * format_->trackBytes(), format_->trackBytes()));
      else ring.assign(format_->trackBits, 0);
      break;
    case disk2::Layout::woz: {
      const disk2::WozTrack* track = wozTrackAt(index);
      if (track != nullptr) ring = disk2::wozTrackBits(image, *track);
      else makeNoise();
      break;
    }
  }
  return ring;
}

void Disk2::advance() {
  const auto target = static_cast<std::int64_t>(static_cast<double>(scheduler_.now()) / ticksPerBit_);
  if (!spinning()) {
    // 盘片没转：位置不重要，读写状态清掉。
    cursor_ = target;
    readShift_ = 0;
    readReady_ = false;
    shiftLeft_ = 0;
    return;
  }
  if (target <= cursor_) return;
  std::vector<std::uint8_t>& ring = trackBits(ringIndex());
  const auto ringBits = static_cast<std::int64_t>(ring.size());
  // 读模式下隔了超过一圈没有访问：只需要转最后一圈。
  if (!q7_ && target - cursor_ > ringBits) {
    cursor_ = target - ringBits;
    readShift_ = 0;
  }
  const bool writable = q7_ && hasDisk() && !drive_->writeProtected();
  for (; cursor_ < target; ++cursor_) {
    const auto position = static_cast<std::size_t>(cursor_ % ringBits);
    if (q7_) {
      std::uint8_t out = 0;
      if (shiftLeft_ > 0) {
        out = static_cast<std::uint8_t>((shiftReg_ >> 7) & 1U);
        shiftReg_ = static_cast<std::uint8_t>(shiftReg_ << 1);
        --shiftLeft_;
      }
      if (!writable) continue;
      ring[position] = out;
      if (!sessionDirty_) {
        sessionDirty_ = true;
        sessionTrack_ = ringIndex();
      }
      continue;
    }
    // 读：数据寄存器从第一个 1 位起收满 8 位，读完的半字节最高位是 1。
    const unsigned bit = ring[position] != 0 ? 1U : 0U;
    if (readShift_ == 0 && bit == 0) continue;
    readShift_ = (readShift_ << 1) | bit;
    if ((readShift_ & 0x80U) != 0) {
      readLatch_ = static_cast<std::uint8_t>(readShift_);
      readReady_ = true;
      readShift_ = 0;
    }
  }
}

std::uint8_t Disk2::readData() {
  if (!spinning() || !readReady_) return 0;
  readReady_ = false;
  return readLatch_;
}

void Disk2::finishWrite() {
  if (!sessionDirty_) return;
  sessionDirty_ = false;
  if (!hasDisk()) return;
  const std::vector<std::uint8_t>& ring = rings_[sessionTrack_];
  switch (format_->layout) {
    case disk2::Layout::sector:
      if (sessionTrack_ >= format_->trackCount) return;
      for (const disk2::DecodedSector& sector : disk2::decodeTrack(*format_, ring)) {
        if (sector.track != sessionTrack_) continue;
        const std::size_t base = (static_cast<std::size_t>(sessionTrack_) * format_->sectorsPerTrack + sector.logical) * disk2::SECTOR_SIZE;
        for (std::size_t index = 0; index < disk2::SECTOR_SIZE; ++index) {
          if (drive_->readByte(base + index) != sector.data[index]) drive_->writeByte(base + index, sector.data[index]);
        }
      }
      break;
    case disk2::Layout::nibble: {
      if (sessionTrack_ >= format_->trackCount) return;
      const std::vector<std::uint8_t> nibbles = disk2::bitsToNibbles(ring, disk2::NIBBLE_TRACK_BYTES);
      const std::size_t base = static_cast<std::size_t>(sessionTrack_) * disk2::NIBBLE_TRACK_BYTES;
      for (std::size_t index = 0; index < nibbles.size(); ++index) {
        if (drive_->readByte(base + index) != nibbles[index]) drive_->writeByte(base + index, nibbles[index]);
      }
      break;
    }
    case disk2::Layout::woz: {
      const disk2::WozTrack* track = wozTrackAt(sessionTrack_);
      if (track == nullptr || ring.size() != track->bitCount) return;
      // 把位反向填回镜像：最后一个字节里不属于这个磁道的位保持原样。
      for (std::size_t byte = 0; byte < (track->bitCount + 7) / 8; ++byte) {
        std::uint8_t value = drive_->readByte(track->offset + byte);
        for (std::size_t bit = 0; bit < 8 && byte * 8 + bit < track->bitCount; ++bit) {
          const auto mask = static_cast<std::uint8_t>(0x80U >> bit);
          value = static_cast<std::uint8_t>(ring[byte * 8 + bit] != 0 ? (value | mask) : (value & ~mask));
        }
        if (drive_->readByte(track->offset + byte) != value) drive_->writeByte(track->offset + byte, value);
      }
      break;
    }
  }
}

void Disk2::onEvent(hw::MasterTime /*when*/, std::uint32_t /*tag*/) {
  motorEvent_ = hw::NO_EVENT;
  advance();
  motorOn_ = false;
  updateMotor();
  // 停转时丢掉编好的磁道：下次转起来从镜像重编，镜像在这期间被外部改动也能看到。
  dropCaches();
}

}  // namespace ember::logic::hardware
