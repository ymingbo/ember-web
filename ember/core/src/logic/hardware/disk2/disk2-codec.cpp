/*
 * ------------------------------------------------------------------------
 * 名称：disk2-codec.cpp
 * 说明：5.25 英寸软盘的磁道编码、解码与盘片格式的识别。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-25
 * 备注：无
 * ------------------------------------------------------------------------
 */

#include "logic/hardware/disk2/disk2-codec.hpp"

#include <algorithm>
#include <iterator>
#include <string_view>

namespace ember::logic::hardware::disk2 {

namespace {

constexpr std::uint8_t ADDRESS_MARK[3] = {0xD5, 0xAA, 0x96};
constexpr std::uint8_t DATA_MARK[3] = {0xD5, 0xAA, 0xAD};
constexpr std::uint8_t TRAILER[3] = {0xDE, 0xAA, 0xEB};
/** 数据场里半字节的个数：86 个辅助、256 个主体。 */
constexpr std::size_t AUX_COUNT = 86;
constexpr std::size_t DATA_NIBBLES = AUX_COUNT + SECTOR_SIZE;
/** 地址场之后最多隔多少个半字节必须出现数据场的起始标记。 */
constexpr std::size_t DATA_SEARCH_LIMIT = 100;

/** 苹果镜像的大小：35 道与 40 道，每道 16 个扇区。 */
constexpr std::size_t APPLE_SIZE_35 = 35 * 16 * SECTOR_SIZE;
constexpr std::size_t APPLE_SIZE_40 = 40 * 16 * SECTOR_SIZE;
/** DOS 3.x 卷目录表在 DOS 顺序镜像里的偏移（第 17 道第 0 扇区）及其中的字段偏移。 */
constexpr std::size_t DOS_VTOC_OFFSET = 17 * 16 * SECTOR_SIZE;
constexpr std::size_t VTOC_CATALOG_TRACK = 0x01;
constexpr std::size_t VTOC_PAIRS_PER_SECTOR = 0x27;
constexpr std::size_t VTOC_TRACKS = 0x34;
constexpr std::size_t VTOC_SECTORS = 0x35;
/** ProDOS 卷目录头所在的块 2 在镜像里的偏移，以及其中的字段偏移。 */
constexpr std::size_t PRODOS_VOLUME_HEADER_OFFSET = 2 * 512;

/** 6 位值换成磁盘半字节。 */
constexpr std::array<std::uint8_t, 64> ENCODE_TABLE = {0x96, 0x97, 0x9A, 0x9B, 0x9D, 0x9E, 0x9F, 0xA6, 0xA7, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF, 0xB2, 0xB3,
                                                       0xB4, 0xB5, 0xB6, 0xB7, 0xB9, 0xBA, 0xBB, 0xBC, 0xBD, 0xBE, 0xBF, 0xCB, 0xCD, 0xCE, 0xCF, 0xD3,
                                                       0xD6, 0xD7, 0xD9, 0xDA, 0xDB, 0xDC, 0xDD, 0xDE, 0xDF, 0xE5, 0xE6, 0xE7, 0xE9, 0xEA, 0xEB, 0xEC,
                                                       0xED, 0xEE, 0xEF, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF9, 0xFA, 0xFB, 0xFC, 0xFD, 0xFE, 0xFF};

/** 磁盘半字节换成 6 位值，不合法的半字节是 0xFF（6 位值最大 63，不会冲突）。 */
constexpr std::array<std::uint8_t, 256> makeDecodeTable() {
  std::array<std::uint8_t, 256> table{};
  for (auto& entry : table) entry = 0xFF;
  for (std::size_t value = 0; value < ENCODE_TABLE.size(); ++value) table[ENCODE_TABLE[value]] = static_cast<std::uint8_t>(value);
  return table;
}
constexpr std::array<std::uint8_t, 256> DECODE_TABLE = makeDecodeTable();

/** 物理扇区号换成逻辑扇区号。 */
std::array<std::uint8_t, MAX_SECTORS_PER_TRACK> physicalToLogical(const Format& format) {
  std::array<std::uint8_t, MAX_SECTORS_PER_TRACK> table{};
  for (std::size_t logical = 0; logical < format.sectorsPerTrack; ++logical) table[format.logicalToPhysical[logical]] = static_cast<std::uint8_t>(logical);
  return table;
}

/** 往位序列里追加半字节。 */
class BitWriter {
 public:
  explicit BitWriter(std::vector<std::uint8_t>& bits) : bits_(bits) {}

  /** 一个半字节：8 位，高位在前。 */
  void nibble(std::uint8_t value) {
    for (int bit = 7; bit >= 0; --bit) bits_.push_back(static_cast<std::uint8_t>((value >> bit) & 1U));
  }

  void marks(const std::uint8_t (&bytes)[3]) {
    for (const std::uint8_t value : bytes) nibble(value);
  }

  /** 同步字节：FF 后面跟两个 0 位。 */
  void sync(std::size_t count) {
    for (std::size_t index = 0; index < count; ++index) {
      nibble(0xFF);
      bits_.push_back(0);
      bits_.push_back(0);
    }
  }

  /** 4 加 4 编码：一个字节拆成两个半字节，奇数位在前。 */
  void fourAndFour(std::uint8_t value) {
    nibble(static_cast<std::uint8_t>((value >> 1) | 0xAA));
    nibble(static_cast<std::uint8_t>(value | 0xAA));
  }

  /** 一个扇区的数据场：起始标记、343 个半字节、收尾标记。 */
  void dataField(std::span<const std::uint8_t> data) {
    // 共 258 次迭代：每个辅助字节收三组两位；最后两次迭代重复取了第 1、第 0 个字节。
    std::array<std::uint8_t, AUX_COUNT> aux{};
    std::array<std::uint8_t, SECTOR_SIZE> body{};
    for (std::size_t step = 0; step < 3 * AUX_COUNT; ++step) {
      const std::size_t index = (1U - step) & 0xFFU;
      const std::uint8_t byte = data[index];
      const std::size_t slot = step % AUX_COUNT;
      aux[slot] = static_cast<std::uint8_t>((aux[slot] << 2) | ((byte & 1U) << 1) | ((byte >> 1) & 1U));
      body[index] = static_cast<std::uint8_t>(byte >> 2);
    }
    for (auto& value : aux) value &= 0x3F;
    marks(DATA_MARK);
    // 辅助部分从最后一个往前，每个半字节是相邻两个值的异或；主体部分接着异或下去。
    std::uint8_t previous = 0;
    for (std::size_t index = AUX_COUNT; index-- > 0;) {
      nibble(ENCODE_TABLE[aux[index] ^ previous]);
      previous = aux[index];
    }
    for (std::size_t index = 0; index < SECTOR_SIZE; ++index) {
      nibble(ENCODE_TABLE[body[index] ^ previous]);
      previous = body[index];
    }
    nibble(ENCODE_TABLE[previous]);
    marks(TRAILER);
  }

 private:
  std::vector<std::uint8_t>& bits_;
};

/** 读出一圈位里的半字节：读头从头转两圈，每个半字节从第一个 1 位开始、8 位一组。 */
std::vector<std::uint8_t> readNibbles(std::span<const std::uint8_t> bits) {
  std::vector<std::uint8_t> nibbles;
  nibbles.reserve(bits.size() / 4);
  unsigned shift = 0;
  for (std::size_t index = 0; index < 2 * bits.size(); ++index) {
    const unsigned bit = bits[index % bits.size()] != 0 ? 1U : 0U;
    if (shift == 0 && bit == 0) continue;
    shift = (shift << 1) | bit;
    if ((shift & 0x80U) != 0) {
      nibbles.push_back(static_cast<std::uint8_t>(shift));
      shift = 0;
    }
  }
  return nibbles;
}

}  // namespace

std::optional<Format> pyramidFormatOf(std::span<const std::uint8_t> image) {
  if (image.size() != PYRAMID_FORMAT.imageSize()) return std::nullopt;
  return PYRAMID_FORMAT;
}

std::optional<Format> appleFormatOf(std::span<const std::uint8_t> image) {
  if (parseWoz(image).has_value()) return Format{TRACK_COUNT, 0, {}, 254, 0, 0, 0, 0, Layout::woz};
  if (image.size() == 35 * NIBBLE_TRACK_BYTES) return nibbleFormat(35);
  if (image.size() == 40 * NIBBLE_TRACK_BYTES) return nibbleFormat(40);
  unsigned trackCount = 0;
  if (image.size() == APPLE_SIZE_35) trackCount = 35;
  else if (image.size() == APPLE_SIZE_40) trackCount = 40;
  else return std::nullopt;

  // DOS 3.x：卷目录表里目录起始磁道是 17，每个扇区 122 对磁道扇区、磁道数与每道扇区数与镜像一致。
  const bool dosOrder = image[DOS_VTOC_OFFSET + VTOC_CATALOG_TRACK] == 17 && image[DOS_VTOC_OFFSET + VTOC_PAIRS_PER_SECTOR] == 122 && image[DOS_VTOC_OFFSET + VTOC_TRACKS] == trackCount &&
                        image[DOS_VTOC_OFFSET + VTOC_SECTORS] == 16;
  if (dosOrder) return appleDosFormat(trackCount);

  // ProDOS：块 2 是卷目录头：前后块号为 0，存储类型（高半字节）是 $F。
  const bool prodosOrder = image[PRODOS_VOLUME_HEADER_OFFSET] == 0 && image[PRODOS_VOLUME_HEADER_OFFSET + 1] == 0 && (image[PRODOS_VOLUME_HEADER_OFFSET + 4] & 0xF0U) == 0xF0U;
  if (prodosOrder) return appleProdosFormat(trackCount);
  return appleDosFormat(trackCount);
}

std::vector<std::uint8_t> encodeTrack(const Format& format, unsigned track, std::span<const std::uint8_t> sectors) {
  std::vector<std::uint8_t> bits;
  bits.reserve(format.trackBits);
  BitWriter writer(bits);
  const auto toLogical = physicalToLogical(format);
  writer.sync(format.gapTrackStart);
  for (unsigned physical = 0; physical < format.sectorsPerTrack; ++physical) {
    writer.marks(ADDRESS_MARK);
    writer.fourAndFour(format.volume);
    writer.fourAndFour(static_cast<std::uint8_t>(track));
    writer.fourAndFour(static_cast<std::uint8_t>(physical));
    writer.fourAndFour(static_cast<std::uint8_t>(format.volume ^ track ^ physical));
    writer.marks(TRAILER);
    writer.sync(format.gapAfterAddress);
    writer.dataField(sectors.subspan(toLogical[physical] * SECTOR_SIZE, SECTOR_SIZE));
    writer.sync(format.gapAfterData);
  }
  // 剩下的位补成同步字节的样子（FF 加两个 0 位，循环）。
  for (std::size_t index = bits.size(); index < format.trackBits; ++index) bits.push_back(index % 10 < 8 ? 1 : 0);
  bits.resize(format.trackBits);
  return bits;
}

std::vector<DecodedSector> decodeTrack(const Format& format, std::span<const std::uint8_t> bits) {
  std::vector<DecodedSector> found;
  if (bits.empty()) return found;
  const auto toLogical = physicalToLogical(format);
  const std::vector<std::uint8_t> nibbles = readNibbles(bits);
  const std::size_t size = nibbles.size();
  const auto at = [&](std::size_t position) { return position < size ? nibbles[position] : std::uint8_t{0}; };
  const auto matches = [&](std::size_t position, const std::uint8_t (&mark)[3]) { return at(position) == mark[0] && at(position + 1) == mark[1] && at(position + 2) == mark[2]; };
  const auto fourAndFour = [&](std::size_t position) { return static_cast<std::uint8_t>(((at(position) << 1) | 1U) & at(position + 1)); };

  for (std::size_t start = 0; start < size; ++start) {
    if (!matches(start, ADDRESS_MARK)) continue;
    const std::uint8_t volume = fourAndFour(start + 3);
    const std::uint8_t track = fourAndFour(start + 5);
    const std::uint8_t sector = fourAndFour(start + 7);
    const std::uint8_t check = fourAndFour(start + 9);
    if (check != static_cast<std::uint8_t>(volume ^ track ^ sector) || sector >= format.sectorsPerTrack) continue;
    // 地址场之后的有限范围里找数据场的起始标记。
    const std::size_t afterAddress = start + 11;
    for (std::size_t probe = afterAddress; probe < afterAddress + DATA_SEARCH_LIMIT; ++probe) {
      if (!matches(probe, DATA_MARK)) continue;
      std::array<std::uint8_t, DATA_NIBBLES + 1> values{};
      bool valid = true;
      for (std::size_t index = 0; index < values.size() && valid; ++index) {
        values[index] = DECODE_TABLE[at(probe + 3 + index)];
        valid = values[index] != 0xFF;
      }
      if (!valid) break;
      std::array<std::uint8_t, AUX_COUNT> aux{};
      std::uint8_t previous = 0;
      for (std::size_t index = 0; index < AUX_COUNT; ++index) {
        previous = static_cast<std::uint8_t>(previous ^ values[index]);
        aux[AUX_COUNT - 1 - index] = previous;
      }
      std::array<std::uint8_t, SECTOR_SIZE> body{};
      previous = aux[0];
      for (std::size_t index = 0; index < SECTOR_SIZE; ++index) {
        previous = static_cast<std::uint8_t>(previous ^ values[AUX_COUNT + index]);
        body[index] = previous;
      }
      if (values[DATA_NIBBLES] != body[SECTOR_SIZE - 1]) break;
      DecodedSector decoded;
      decoded.track = track;
      decoded.physical = sector;
      decoded.logical = toLogical[sector];
      for (std::size_t index = 0; index < SECTOR_SIZE; ++index) {
        const std::size_t step = (1U - index) & 0xFFU;
        const std::size_t pass = step / AUX_COUNT;
        const unsigned group = (aux[step % AUX_COUNT] >> ((2 - pass) * 2)) & 3U;
        const unsigned low = ((group & 1U) << 1) | (group >> 1);
        decoded.data[index] = static_cast<std::uint8_t>((body[index] << 2) | low);
      }
      const auto same = std::find_if(found.begin(), found.end(), [&](const DecodedSector& item) { return item.physical == decoded.physical; });
      if (same != found.end()) *same = decoded;
      else found.push_back(decoded);
      break;
    }
  }
  return found;
}

namespace {

constexpr std::size_t WOZ_HEADER_SIZE = 12;
/** WOZ 文件头的签名后面的 4 个字节：FF 0A 0D 0A。 */
constexpr std::uint8_t WOZ_SIGNATURE_TAIL[4] = {0xFF, 0x0A, 0x0D, 0x0A};
constexpr std::size_t WOZ_TRACK_COUNT = 160;
constexpr std::uint8_t WOZ_NO_TRACK = 0xFF;
/** INFO 块里“盘片类型”字段的偏移与 5.25 英寸盘的值。 */
constexpr std::size_t WOZ_INFO_DISK_TYPE = 1;
constexpr std::uint8_t WOZ_DISK_TYPE_525 = 1;
/** WOZ1：每个磁道记录 6656 字节，位序列 6646 字节，随后是已用字节数与位数。 */
constexpr std::size_t WOZ1_RECORD_SIZE = 6656;
constexpr std::size_t WOZ1_BITS_BYTES = 6646;
/** WOZ2：磁道表每项 8 字节（起始块、块数、位数），块是 512 字节。 */
constexpr std::size_t WOZ2_ENTRY_SIZE = 8;
constexpr std::size_t WOZ2_BLOCK_SIZE = 512;

std::uint32_t readLittleEndian(std::span<const std::uint8_t> bytes, std::size_t offset, std::size_t width) {
  std::uint32_t value = 0;
  for (std::size_t index = 0; index < width; ++index) value |= static_cast<std::uint32_t>(bytes[offset + index]) << (8 * index);
  return value;
}

}  // namespace

std::optional<WozImage> parseWoz(std::span<const std::uint8_t> image) {
  if (image.size() < WOZ_HEADER_SIZE || image[0] != 'W' || image[1] != 'O' || image[2] != 'Z') return std::nullopt;
  const bool second = image[3] == '2';
  if (!second && image[3] != '1') return std::nullopt;
  if (!std::equal(std::begin(WOZ_SIGNATURE_TAIL), std::end(WOZ_SIGNATURE_TAIL), image.begin() + 4)) return std::nullopt;

  std::optional<std::span<const std::uint8_t>> info;
  std::optional<std::span<const std::uint8_t>> map;
  std::optional<std::span<const std::uint8_t>> tracks;
  std::size_t position = WOZ_HEADER_SIZE;
  while (position + 8 <= image.size()) {
    const std::size_t size = readLittleEndian(image, position + 4, 4);
    if (size > image.size() - position - 8) break;
    const std::span<const std::uint8_t> body = image.subspan(position + 8, size);
    const std::string_view id(reinterpret_cast<const char*>(image.data() + position), 4);
    if (id == "INFO") info = body;
    else if (id == "TMAP") map = body;
    else if (id == "TRKS") tracks = body;
    position += 8 + size;
  }
  if (!info.has_value() || !map.has_value() || !tracks.has_value() || info->size() <= WOZ_INFO_DISK_TYPE || map->size() < WOZ_TRACK_COUNT) return std::nullopt;
  if ((*info)[WOZ_INFO_DISK_TYPE] != WOZ_DISK_TYPE_525) return std::nullopt;

  WozImage result;
  const std::size_t tracksOffset = static_cast<std::size_t>(tracks->data() - image.data());
  const std::size_t entryCount = second ? std::min<std::size_t>(WOZ_TRACK_COUNT, tracks->size() / WOZ2_ENTRY_SIZE) : std::min<std::size_t>(WOZ_TRACK_COUNT, tracks->size() / WOZ1_RECORD_SIZE);
  // WOZ2 的磁道表占 160 项，位序列在文件的块里；WOZ1 的每个磁道记录紧挨着。
  const std::size_t dataStart = second ? WOZ_TRACK_COUNT * WOZ2_ENTRY_SIZE : 0;
  for (std::size_t index = 0; index < entryCount; ++index) {
    WozTrack track;
    if (second) {
      const std::size_t block = readLittleEndian(*tracks, index * WOZ2_ENTRY_SIZE, 2);
      track.bitCount = readLittleEndian(*tracks, index * WOZ2_ENTRY_SIZE + 4, 4);
      track.offset = block * WOZ2_BLOCK_SIZE;
      if (block == 0) track.bitCount = 0;
    } else {
      const std::size_t record = dataStart + index * WOZ1_RECORD_SIZE;
      track.offset = tracksOffset + record;
      track.bitCount = std::min<std::size_t>(readLittleEndian(*tracks, record + WOZ1_BITS_BYTES + 2, 2), WOZ1_BITS_BYTES * 8);
    }
    // 超出文件的磁道丢掉。
    if (track.bitCount == 0 || track.offset > image.size() || (track.bitCount + 7) / 8 > image.size() - track.offset) track.bitCount = 0;
    result.tracks.push_back(track);
  }
  for (std::size_t quarter = 0; quarter < WOZ_TRACK_COUNT; ++quarter) {
    const std::uint8_t index = (*map)[quarter];
    result.trackMap[quarter] = (index != WOZ_NO_TRACK && index < result.tracks.size() && result.tracks[index].bitCount > 0) ? static_cast<std::int16_t>(index) : std::int16_t{-1};
  }
  return result;
}

std::vector<std::uint8_t> nibblesToBits(std::span<const std::uint8_t> nibbles) {
  std::vector<std::uint8_t> bits;
  bits.reserve(nibbles.size() * 8);
  for (const std::uint8_t value : nibbles) {
    for (int bit = 7; bit >= 0; --bit) bits.push_back(static_cast<std::uint8_t>((value >> bit) & 1U));
  }
  return bits;
}

std::vector<std::uint8_t> bitsToNibbles(std::span<const std::uint8_t> bits, std::size_t count) {
  std::vector<std::uint8_t> nibbles;
  nibbles.reserve(count);
  unsigned shift = 0;
  for (std::size_t index = 0; index < bits.size() && nibbles.size() < count; ++index) {
    const unsigned bit = bits[index] != 0 ? 1U : 0U;
    if (shift == 0 && bit == 0) continue;
    shift = (shift << 1) | bit;
    if ((shift & 0x80U) != 0) {
      nibbles.push_back(static_cast<std::uint8_t>(shift));
      shift = 0;
    }
  }
  nibbles.resize(count, 0xFF);
  return nibbles;
}

std::vector<std::uint8_t> wozTrackBits(std::span<const std::uint8_t> image, const WozTrack& track) {
  std::vector<std::uint8_t> bits(track.bitCount);
  for (std::size_t index = 0; index < track.bitCount; ++index) bits[index] = (image[track.offset + index / 8] >> (7 - index % 8)) & 1U;
  return bits;
}

}  // namespace ember::logic::hardware::disk2
