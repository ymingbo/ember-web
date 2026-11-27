/*
 * ------------------------------------------------------------------------
 * 名称：apple-image.cpp
 * 说明：苹果软盘镜像的扇区顺序换算与文件系统识别的实现。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-25
 * 备注：无。
 * ------------------------------------------------------------------------
 */

#include "logic/filemanager/apple-image.hpp"

#include <algorithm>
#include <array>
#include <cstddef>

#include "logic/filemanager/disk-text.hpp"
#include "shared/disk/apple-sector-order.hpp"

namespace ember::logic::filemanager {

namespace {

constexpr std::size_t IMAGE_SIZE_35 = 35 * APPLE_SECTORS * APPLE_SECTOR_SIZE;
constexpr std::size_t IMAGE_SIZE_40 = 40 * APPLE_SECTORS * APPLE_SECTOR_SIZE;
/** DOS 3.3 卷目录表在第 17 道第 0 扇区。 */
constexpr unsigned VTOC_TRACK = 17;
/** ProDOS 的卷目录头在块 2。 */
constexpr unsigned PRODOS_KEY_BLOCK = 2;
constexpr std::size_t PRODOS_BLOCK_SIZE = 512;

using Table = std::array<std::uint8_t, shared::APPLE_SECTORS_PER_TRACK>;

constexpr Table invert(const Table& table) {
  Table result{};
  for (std::size_t logical = 0; logical < table.size(); ++logical) result[table[logical]] = static_cast<std::uint8_t>(logical);
  return result;
}

constexpr Table DOS_INVERSE = invert(shared::APPLE_DOS_ORDER);
constexpr Table PRODOS_INVERSE = invert(shared::APPLE_PRODOS_ORDER);

const Table& orderTable(AppleOrder order) { return order == AppleOrder::Dos ? shared::APPLE_DOS_ORDER : shared::APPLE_PRODOS_ORDER; }
const Table& inverseTable(AppleOrder order) { return order == AppleOrder::Dos ? DOS_INVERSE : PRODOS_INVERSE; }

/** 文件系统顺序里第 track 道第 k 个扇区在镜像文件里的偏移。 */
std::size_t imageOffset(unsigned track, unsigned k, AppleOrder fileOrder, AppleOrder fsOrder) {
  const unsigned physical = orderTable(fsOrder)[k];
  const unsigned fileSector = inverseTable(fileOrder)[physical];
  return (static_cast<std::size_t>(track) * APPLE_SECTORS + fileSector) * APPLE_SECTOR_SIZE;
}

bool dosVtocOk(const std::vector<std::uint8_t>& image, AppleOrder fileOrder, unsigned tracks) {
  const std::uint8_t* sector = image.data() + imageOffset(VTOC_TRACK, 0, fileOrder, AppleOrder::Dos);
  return sector[1] == VTOC_TRACK && sector[0x27] == 122 && sector[0x34] == tracks && sector[0x35] == APPLE_SECTORS;
}

bool proDosHeaderOk(const std::vector<std::uint8_t>& image, AppleOrder fileOrder, unsigned tracks) {
  std::array<std::uint8_t, PRODOS_BLOCK_SIZE> block{};
  const unsigned track = PRODOS_KEY_BLOCK / 8;
  const unsigned first = 2 * (PRODOS_KEY_BLOCK % 8);
  const std::uint8_t* low = image.data() + imageOffset(track, first, fileOrder, AppleOrder::ProDos);
  const std::uint8_t* high = image.data() + imageOffset(track, first + 1, fileOrder, AppleOrder::ProDos);
  std::copy(low, low + APPLE_SECTOR_SIZE, block.begin());
  std::copy(high, high + APPLE_SECTOR_SIZE, block.begin() + APPLE_SECTOR_SIZE);
  const unsigned nameLength = block[4] & 0x0FU;
  const std::uint32_t totalBlocks = readLe16(block.data() + 0x29);
  return block[0] == 0 && block[1] == 0 && readLe16(block.data() + 2) == PRODOS_KEY_BLOCK + 1 && (block[4] >> 4) == 0xF && nameLength >= 1 && block[0x23] == 0x27 && block[0x24] == 0x0D &&
         totalBlocks >= 1 && totalBlocks <= tracks * 8;
}

}  // namespace

unsigned appleTrackCount(std::size_t imageSize) {
  if (imageSize == IMAGE_SIZE_35) return 35;
  if (imageSize == IMAGE_SIZE_40) return 40;
  return 0;
}

AppleIdentity identifyApple(const std::vector<std::uint8_t>& image, std::string_view extension) {
  const unsigned tracks = appleTrackCount(image.size());
  if (tracks == 0) return {};
  const AppleOrder preferred = extension == "po" ? AppleOrder::ProDos : AppleOrder::Dos;
  const AppleOrder other = preferred == AppleOrder::Dos ? AppleOrder::ProDos : AppleOrder::Dos;
  for (const AppleOrder order : {preferred, other}) {
    if (proDosHeaderOk(image, order, tracks)) return AppleIdentity{AppleKind::ProDos, order, tracks};
    if (dosVtocOk(image, order, tracks)) return AppleIdentity{AppleKind::Dos33, order, tracks};
  }
  return {};
}

std::vector<std::uint8_t> appleToLinear(const std::vector<std::uint8_t>& image, AppleOrder fileOrder, AppleOrder fsOrder, unsigned tracks) {
  std::vector<std::uint8_t> linear(static_cast<std::size_t>(tracks) * APPLE_SECTORS * APPLE_SECTOR_SIZE);
  for (unsigned track = 0; track < tracks; ++track) {
    for (unsigned k = 0; k < APPLE_SECTORS; ++k) {
      const auto begin = image.begin() + static_cast<std::ptrdiff_t>(imageOffset(track, k, fileOrder, fsOrder));
      std::copy(begin, begin + static_cast<std::ptrdiff_t>(APPLE_SECTOR_SIZE), linear.begin() + static_cast<std::ptrdiff_t>((static_cast<std::size_t>(track) * APPLE_SECTORS + k) * APPLE_SECTOR_SIZE));
    }
  }
  return linear;
}

void appleFromLinear(std::vector<std::uint8_t>& image, const std::vector<std::uint8_t>& linear, AppleOrder fileOrder, AppleOrder fsOrder, unsigned tracks) {
  for (unsigned track = 0; track < tracks; ++track) {
    for (unsigned k = 0; k < APPLE_SECTORS; ++k) {
      const auto begin = linear.begin() + static_cast<std::ptrdiff_t>((static_cast<std::size_t>(track) * APPLE_SECTORS + k) * APPLE_SECTOR_SIZE);
      std::copy(begin, begin + static_cast<std::ptrdiff_t>(APPLE_SECTOR_SIZE), image.begin() + static_cast<std::ptrdiff_t>(imageOffset(track, k, fileOrder, fsOrder)));
    }
  }
}

}  // namespace ember::logic::filemanager
