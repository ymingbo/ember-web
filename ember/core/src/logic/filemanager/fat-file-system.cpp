/*
 * ------------------------------------------------------------------------
 * 名称：fat-file-system.cpp
 * 说明：FAT12 与 FAT16 文件系统的实现。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-25
 * 备注：无。
 * ------------------------------------------------------------------------
 */

#include "logic/filemanager/fat-file-system.hpp"

#include <algorithm>
#include <cstring>

#include "contracts/filemanager/filemanager-error.hpp"
#include "logic/filemanager/disk-text.hpp"
#include "shared/errors/core-error.hpp"

namespace ember::logic::filemanager {

namespace api = contracts::filemanager;

namespace {

constexpr std::size_t ENTRY_SIZE = 32;
constexpr std::uint8_t ATTR_READ_ONLY = 0x01;
constexpr std::uint8_t ATTR_VOLUME = 0x08;
constexpr std::uint8_t ATTR_DIRECTORY = 0x10;
constexpr std::uint8_t ATTR_ARCHIVE = 0x20;
constexpr std::uint8_t ATTR_LONG_NAME = 0x0F;
constexpr std::uint8_t ENTRY_FREE = 0xE5;
constexpr std::uint8_t ENTRY_END = 0x00;
constexpr std::uint8_t LFN_LAST = 0x40;
constexpr std::size_t LFN_CHARS_PER_ENTRY = 13;
/** 一个长文件名目录项里 13 个字符所在的字节位置。 */
constexpr std::size_t LFN_CHAR_OFFSETS[LFN_CHARS_PER_ENTRY] = {1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30};
/** 递归删除文件夹的最大深度，防止损坏的磁盘里循环引用。 */
constexpr int MAX_DEPTH = 32;
constexpr const char* NAME_RULE = "FAT 名称必须是 8.3 格式：主名最多 8 个字符、扩展名最多 3 个字符，只能用字母、数字和 $%'-_@~!(){}^#&";

[[noreturn]] void fail(std::string_view code, const std::string& message) { throw shared::CoreError(code, message); }

/** 长文件名里的 UTF-16 换成 UTF-8。 */
std::string utf16ToUtf8(const std::vector<char16_t>& units) {
  std::string out;
  for (std::size_t i = 0; i < units.size(); ++i) {
    char32_t codePoint = units[i];
    if (codePoint >= 0xD800 && codePoint <= 0xDBFF && i + 1 < units.size() && units[i + 1] >= 0xDC00 && units[i + 1] <= 0xDFFF) {
      codePoint = 0x10000 + ((codePoint - 0xD800) << 10) + (units[i + 1] - 0xDC00);
      ++i;
    } else if (codePoint >= 0xD800 && codePoint <= 0xDFFF) {
      codePoint = 0xFFFD;
    }
    if (codePoint < 0x80) {
      out.push_back(static_cast<char>(codePoint));
    } else if (codePoint < 0x800) {
      out.push_back(static_cast<char>(0xC0 | (codePoint >> 6)));
      out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
    } else if (codePoint < 0x10000) {
      out.push_back(static_cast<char>(0xE0 | (codePoint >> 12)));
      out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
    } else {
      out.push_back(static_cast<char>(0xF0 | (codePoint >> 18)));
      out.push_back(static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
    }
  }
  return out;
}

/** 长文件名目录项里记的 8.3 名称校验和。 */
std::uint8_t lfnChecksum(const std::array<std::uint8_t, 11>& shortName) {
  std::uint8_t sum = 0;
  for (const std::uint8_t byte : shortName) sum = static_cast<std::uint8_t>(((sum & 1U) << 7) + (sum >> 1) + byte);
  return sum;
}

/** 8.3 名称的显示文字：主名加点加扩展名。 */
std::string shortNameText(const std::array<std::uint8_t, 11>& shortName) {
  std::string main(reinterpret_cast<const char*>(shortName.data()), 8);
  std::string ext(reinterpret_cast<const char*>(shortName.data()) + 8, 3);
  if (!main.empty() && static_cast<unsigned char>(main[0]) == 0x05) main[0] = static_cast<char>(0xE5);
  while (!main.empty() && main.back() == ' ') main.pop_back();
  while (!ext.empty() && ext.back() == ' ') ext.pop_back();
  return oemToUtf8(ext.empty() ? main : main + "." + ext);
}

/** 字符能否用在 8.3 名称里。 */
bool isShortNameChar(char c) {
  if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) return true;
  return std::strchr("$%'-_@~`!(){}^#&", c) != nullptr;
}

/** 用户给的名称换成 8.3 名称的 11 个字节；不合格时抛出 invalidName。 */
std::array<std::uint8_t, 11> parseShortName(const std::string& input) {
  if (input.empty() || !isPrintableAscii(input)) fail(api::ERROR_INVALID_NAME, NAME_RULE);
  const std::string name = upperAscii(input);
  const std::size_t dot = name.rfind('.');
  const std::string main = dot == std::string::npos ? name : name.substr(0, dot);
  const std::string ext = dot == std::string::npos ? std::string() : name.substr(dot + 1);
  if (main.empty() || main.size() > 8 || ext.size() > 3 || (dot != std::string::npos && ext.empty())) fail(api::ERROR_INVALID_NAME, NAME_RULE);
  if (!std::all_of(main.begin(), main.end(), isShortNameChar) || !std::all_of(ext.begin(), ext.end(), isShortNameChar)) fail(api::ERROR_INVALID_NAME, NAME_RULE);
  std::array<std::uint8_t, 11> result;
  result.fill(' ');
  std::copy(main.begin(), main.end(), result.begin());
  std::copy(ext.begin(), ext.end(), result.begin() + 8);
  return result;
}

std::uint16_t encodeDate(const LocalTime& time) {
  const int year = std::clamp(time.year, 1980, 2107);
  return static_cast<std::uint16_t>(((year - 1980) << 9) | (time.month << 5) | time.day);
}

std::uint16_t encodeTime(const LocalTime& time) { return static_cast<std::uint16_t>((time.hour << 11) | (time.minute << 5) | (time.second / 2)); }

/** 目录项里的日期时间写成显示文字；没有日期时为空。 */
std::string modifiedText(std::uint16_t date, std::uint16_t time) {
  if (date == 0) return {};
  LocalTime text;
  text.year = 1980 + (date >> 9);
  text.month = (date >> 5) & 0x0F;
  text.day = date & 0x1F;
  text.hour = time >> 11;
  text.minute = (time >> 5) & 0x3F;
  text.second = (time & 0x1F) * 2;
  return formatTime(text, false);
}

}  // namespace

std::unique_ptr<FatFileSystem> FatFileSystem::open(std::vector<std::uint8_t>& image) {
  if (image.size() < 512) return nullptr;
  const std::uint8_t* boot = image.data();
  Geometry g;
  g.bytesPerSector = readLe16(boot + 0x0B);
  g.sectorsPerCluster = boot[0x0D];
  const std::uint32_t reserved = readLe16(boot + 0x0E);
  g.fatCount = boot[0x10];
  g.rootEntries = readLe16(boot + 0x11);
  std::uint32_t total = readLe16(boot + 0x13);
  if (total == 0) total = readLe32(boot + 0x20);
  g.fatSectors = readLe16(boot + 0x16);
  const bool bpsOk = g.bytesPerSector >= 512 && g.bytesPerSector <= 4096 && (g.bytesPerSector & (g.bytesPerSector - 1)) == 0;
  const bool spcOk = g.sectorsPerCluster != 0 && g.sectorsPerCluster <= 128 && (g.sectorsPerCluster & (g.sectorsPerCluster - 1)) == 0;
  if (!bpsOk || !spcOk || reserved == 0 || g.fatCount == 0 || g.fatCount > 2 || g.rootEntries == 0 || g.fatSectors == 0 || total == 0) return nullptr;
  const bool signature = boot[510] == 0x55 && boot[511] == 0xAA;
  if (!signature && boot[0] != 0xEB && boot[0] != 0xE9) return nullptr;
  if (static_cast<std::uint64_t>(total) * g.bytesPerSector > image.size()) return nullptr;
  g.fatStart = reserved;
  g.rootStart = reserved + g.fatCount * g.fatSectors;
  const std::uint32_t rootSectors = (g.rootEntries * static_cast<std::uint32_t>(ENTRY_SIZE) + g.bytesPerSector - 1) / g.bytesPerSector;
  g.dataStart = g.rootStart + rootSectors;
  if (g.dataStart >= total) return nullptr;
  g.clusterCount = (total - g.dataStart) / g.sectorsPerCluster;
  if (g.clusterCount == 0 || g.clusterCount >= 65525) return nullptr;
  g.fat12 = g.clusterCount < 4085;
  const std::uint64_t fatBytesNeeded = g.fat12 ? ((static_cast<std::uint64_t>(g.clusterCount) + 2) * 3 + 1) / 2 : (static_cast<std::uint64_t>(g.clusterCount) + 2) * 2;
  if (fatBytesNeeded > static_cast<std::uint64_t>(g.fatSectors) * g.bytesPerSector) return nullptr;
  if (image[static_cast<std::size_t>(g.fatStart) * g.bytesPerSector] < 0xF0) return nullptr;
  return std::unique_ptr<FatFileSystem>(new FatFileSystem(image, g));
}

FatFileSystem::FatFileSystem(std::vector<std::uint8_t>& image, const Geometry& geometry) : image_(image), geometry_(geometry) {
  traits_.formatId = geometry.fat12 ? "fat12" : "fat16";
  traits_.formatName = geometry.fat12 ? "FAT12" : "FAT16";
  traits_.writable = true;
  traits_.subdirectories = true;
}

// ---------------------------------------------------------------- FAT 表

std::uint32_t FatFileSystem::fatGet(std::uint32_t cluster) const {
  const std::size_t base = static_cast<std::size_t>(geometry_.fatStart) * geometry_.bytesPerSector;
  const std::size_t limit = static_cast<std::size_t>(geometry_.fatSectors) * geometry_.bytesPerSector;
  if (geometry_.fat12) {
    const std::size_t index = cluster + cluster / 2;
    if (index + 1 >= limit) fail(api::ERROR_CORRUPT, "磁盘的 FAT 表已损坏");
    const std::uint32_t word = readLe16(image_.data() + base + index);
    return (cluster & 1U) != 0 ? word >> 4 : word & 0x0FFFU;
  }
  const std::size_t index = static_cast<std::size_t>(cluster) * 2;
  if (index + 1 >= limit) fail(api::ERROR_CORRUPT, "磁盘的 FAT 表已损坏");
  return readLe16(image_.data() + base + index);
}

void FatFileSystem::fatSet(std::uint32_t cluster, std::uint32_t value) {
  const std::size_t limit = static_cast<std::size_t>(geometry_.fatSectors) * geometry_.bytesPerSector;
  for (std::uint32_t copy = 0; copy < geometry_.fatCount; ++copy) {
    const std::size_t base = (static_cast<std::size_t>(geometry_.fatStart) + static_cast<std::size_t>(copy) * geometry_.fatSectors) * geometry_.bytesPerSector;
    if (geometry_.fat12) {
      const std::size_t index = cluster + cluster / 2;
      if (index + 1 >= limit) fail(api::ERROR_CORRUPT, "磁盘的 FAT 表已损坏");
      std::uint8_t* word = image_.data() + base + index;
      const std::uint32_t old = readLe16(word);
      const std::uint32_t updated = (cluster & 1U) != 0 ? (old & 0x000FU) | ((value & 0x0FFFU) << 4) : (old & 0xF000U) | (value & 0x0FFFU);
      writeLe16(word, updated);
    } else {
      const std::size_t index = static_cast<std::size_t>(cluster) * 2;
      if (index + 1 >= limit) fail(api::ERROR_CORRUPT, "磁盘的 FAT 表已损坏");
      writeLe16(image_.data() + base + index, value);
    }
  }
}

std::size_t FatFileSystem::clusterOffset(std::uint32_t cluster) const {
  if (cluster < 2 || cluster >= geometry_.clusterCount + 2) fail(api::ERROR_CORRUPT, "磁盘的簇号超出范围，文件系统已损坏");
  return (static_cast<std::size_t>(geometry_.dataStart) + static_cast<std::size_t>(cluster - 2) * geometry_.sectorsPerCluster) * geometry_.bytesPerSector;
}

std::vector<std::uint32_t> FatFileSystem::chain(std::uint32_t first) const {
  std::vector<std::uint32_t> result;
  std::uint32_t cluster = first;
  while (true) {
    if (cluster < 2 || cluster >= geometry_.clusterCount + 2 || result.size() > geometry_.clusterCount) fail(api::ERROR_CORRUPT, "磁盘的簇链已损坏");
    result.push_back(cluster);
    const std::uint32_t next = fatGet(cluster);
    if (isEndOfChain(next)) break;
    cluster = next;
  }
  return result;
}

std::vector<std::uint32_t> FatFileSystem::allocateChain(std::size_t count) {
  std::vector<std::uint32_t> clusters;
  if (count == 0) return clusters;
  for (std::uint32_t cluster = 2; cluster < geometry_.clusterCount + 2 && clusters.size() < count; ++cluster) {
    if (fatGet(cluster) == 0) clusters.push_back(cluster);
  }
  if (clusters.size() < count) fail(api::ERROR_NO_SPACE, "磁盘空间不足");
  for (std::size_t i = 0; i < clusters.size(); ++i) fatSet(clusters[i], i + 1 < clusters.size() ? clusters[i + 1] : endOfChain());
  return clusters;
}

void FatFileSystem::freeChain(std::uint32_t first) {
  if (first < 2) return;
  for (const std::uint32_t cluster : chain(first)) fatSet(cluster, 0);
}

api::FileSystemSpace FatFileSystem::space() {
  std::uint64_t freeClusters = 0;
  for (std::uint32_t cluster = 2; cluster < geometry_.clusterCount + 2; ++cluster) {
    if (fatGet(cluster) == 0) ++freeClusters;
  }
  return api::FileSystemSpace{static_cast<std::uint64_t>(geometry_.clusterCount) * geometry_.clusterBytes(), freeClusters * geometry_.clusterBytes()};
}

// ---------------------------------------------------------------- 目录

std::vector<std::size_t> FatFileSystem::slots(const DirRef& directory) const {
  std::vector<std::size_t> result;
  if (directory.root) {
    const std::size_t base = static_cast<std::size_t>(geometry_.rootStart) * geometry_.bytesPerSector;
    for (std::uint32_t i = 0; i < geometry_.rootEntries; ++i) result.push_back(base + static_cast<std::size_t>(i) * ENTRY_SIZE);
    return result;
  }
  for (const std::uint32_t cluster : chain(directory.cluster)) {
    const std::size_t base = clusterOffset(cluster);
    for (std::size_t i = 0; i < geometry_.clusterBytes() / ENTRY_SIZE; ++i) result.push_back(base + i * ENTRY_SIZE);
  }
  return result;
}

std::vector<FatFileSystem::Item> FatFileSystem::readDirectory(const DirRef& directory) const {
  std::vector<Item> items;
  std::vector<char16_t> longName;
  std::vector<std::size_t> longOffsets;
  std::uint8_t longChecksum = 0;
  std::uint32_t nextSequence = 0;
  bool longValid = false;
  const auto resetLong = [&] {
    longName.clear();
    longOffsets.clear();
    longValid = false;
    nextSequence = 0;
  };
  for (const std::size_t offset : slots(directory)) {
    const std::uint8_t* entry = image_.data() + offset;
    if (entry[0] == ENTRY_END) break;
    if (entry[0] == ENTRY_FREE) {
      resetLong();
      continue;
    }
    const std::uint8_t attr = entry[11];
    if ((attr & 0x3FU) == ATTR_LONG_NAME) {
      const std::uint32_t sequence = entry[0] & 0x1FU;
      if ((entry[0] & LFN_LAST) != 0) {
        resetLong();
        if (sequence == 0 || sequence > 20) continue;
        longName.assign(static_cast<std::size_t>(sequence) * LFN_CHARS_PER_ENTRY, 0xFFFF);
        longChecksum = entry[13];
        longValid = true;
      } else if (!longValid || sequence != nextSequence) {
        resetLong();
        continue;
      }
      for (std::size_t i = 0; i < LFN_CHARS_PER_ENTRY; ++i) longName[(sequence - 1) * LFN_CHARS_PER_ENTRY + i] = static_cast<char16_t>(readLe16(entry + LFN_CHAR_OFFSETS[i]));
      longOffsets.push_back(offset);
      nextSequence = sequence - 1;
      continue;
    }
    if ((attr & ATTR_VOLUME) != 0 && (attr & ATTR_DIRECTORY) == 0) {
      resetLong();
      continue;
    }
    if (entry[0] == '.') {
      resetLong();
      continue;
    }
    Item item;
    std::copy(entry, entry + 11, item.shortName.begin());
    item.attr = attr;
    item.writeTime = readLe16(entry + 22);
    item.writeDate = readLe16(entry + 24);
    item.cluster = readLe16(entry + 26);
    item.size = readLe32(entry + 28);
    item.offset = offset;
    item.shortDisplay = shortNameText(item.shortName);
    item.display = item.shortDisplay;
    if (longValid && nextSequence == 0 && longChecksum == lfnChecksum(item.shortName)) {
      std::vector<char16_t> units;
      for (const char16_t unit : longName) {
        if (unit == 0 || unit == 0xFFFF) break;
        units.push_back(unit);
      }
      if (!units.empty()) {
        item.display = utf16ToUtf8(units);
        item.lfnOffsets = longOffsets;
      }
    }
    resetLong();
    items.push_back(std::move(item));
  }
  return items;
}

const FatFileSystem::Item* FatFileSystem::findItem(const std::vector<Item>& items, const std::string& name) {
  for (const Item& item : items) {
    if (equalsIgnoreCase(item.display, name) || equalsIgnoreCase(item.shortDisplay, name)) return &item;
  }
  return nullptr;
}

FatFileSystem::DirRef FatFileSystem::resolveDirectory(const api::EntryPath& path) const {
  DirRef current{true, 0};
  for (const std::string& name : path) {
    const std::vector<Item> items = readDirectory(current);
    const Item* item = findItem(items, name);
    if (item == nullptr || !item->directory()) fail(api::ERROR_ENTRY_NOT_FOUND, "找不到文件夹 " + name);
    if (item->cluster < 2) fail(api::ERROR_CORRUPT, "文件夹 " + name + " 的簇号无效，文件系统已损坏");
    current = DirRef{false, item->cluster};
  }
  return current;
}

FatFileSystem::Item FatFileSystem::resolveItem(const api::EntryPath& path, DirRef& parent) const {
  if (path.empty()) fail(api::ERROR_ENTRY_NOT_FOUND, "没有指定条目");
  parent = resolveDirectory(api::EntryPath(path.begin(), path.end() - 1));
  const std::vector<Item> items = readDirectory(parent);
  const Item* item = findItem(items, path.back());
  if (item == nullptr) fail(api::ERROR_ENTRY_NOT_FOUND, "找不到 " + path.back());
  return *item;
}

std::vector<std::size_t> FatFileSystem::findFreeSlots(const DirRef& directory, std::size_t count) {
  while (true) {
    const std::vector<std::size_t> all = slots(directory);
    std::size_t start = all.size();
    std::size_t runLength = 0;
    std::size_t runStart = 0;
    for (std::size_t i = 0; i < all.size(); ++i) {
      const std::uint8_t first = image_[all[i]];
      if (first == ENTRY_END) {
        // 目录的结束标记：它和后面的目录项都是空的
        start = runLength > 0 ? runStart : i;
        break;
      }
      if (first == ENTRY_FREE) {
        if (runLength == 0) runStart = i;
        ++runLength;
        if (runLength == count) return std::vector<std::size_t>(all.begin() + static_cast<std::ptrdiff_t>(runStart), all.begin() + static_cast<std::ptrdiff_t>(runStart + count));
      } else {
        runLength = 0;
      }
    }
    if (start == all.size() && runLength > 0) start = runStart;
    if (all.size() - start >= count) return std::vector<std::size_t>(all.begin() + static_cast<std::ptrdiff_t>(start), all.begin() + static_cast<std::ptrdiff_t>(start + count));
    // 放不下：子目录接一个新簇，根目录是固定大小的
    if (directory.root) fail(api::ERROR_NO_SPACE, "根目录已满");
    const std::vector<std::uint32_t> existing = chain(directory.cluster);
    const std::uint32_t added = allocateChain(1).front();
    fatSet(existing.back(), added);
    std::fill_n(image_.begin() + static_cast<std::ptrdiff_t>(clusterOffset(added)), geometry_.clusterBytes(), static_cast<std::uint8_t>(0));
  }
}

void FatFileSystem::writeEntry(std::size_t offset, const std::array<std::uint8_t, 11>& shortName, std::uint8_t attr, std::uint32_t cluster, std::uint32_t size) {
  std::uint8_t* entry = image_.data() + offset;
  std::fill_n(entry, ENTRY_SIZE, static_cast<std::uint8_t>(0));
  std::copy(shortName.begin(), shortName.end(), entry);
  entry[11] = attr;
  const LocalTime now = currentLocalTime();
  writeLe16(entry + 14, encodeTime(now));
  writeLe16(entry + 16, encodeDate(now));
  writeLe16(entry + 18, encodeDate(now));
  writeLe16(entry + 22, encodeTime(now));
  writeLe16(entry + 24, encodeDate(now));
  writeLe16(entry + 26, cluster);
  writeLe32(entry + 28, size);
}

void FatFileSystem::deleteSlots(const Item& item) {
  for (const std::size_t offset : item.lfnOffsets) image_[offset] = ENTRY_FREE;
  image_[item.offset] = ENTRY_FREE;
}

void FatFileSystem::removeItem(const Item& item, int depth) {
  if (depth > MAX_DEPTH) fail(api::ERROR_CORRUPT, "文件夹层数过多，文件系统可能已损坏");
  if (item.directory() && item.cluster >= 2) {
    for (const Item& child : readDirectory(DirRef{false, item.cluster})) removeItem(child, depth + 1);
  }
  if (item.cluster >= 2) freeChain(item.cluster);
  deleteSlots(item);
}

void FatFileSystem::ensureNameFree(const std::vector<Item>& items, const std::array<std::uint8_t, 11>& shortName, const Item* ignore) const {
  const std::string text = shortNameText(shortName);
  for (const Item& item : items) {
    if (ignore != nullptr && item.offset == ignore->offset) continue;
    if (equalsIgnoreCase(item.shortDisplay, text) || equalsIgnoreCase(item.display, text)) fail(api::ERROR_EXISTS, "已有同名条目 " + text);
  }
}

std::array<std::uint8_t, 11> FatFileSystem::importShortName(const std::string& windowsName, const std::vector<Item>& items) const {
  const std::string upper = upperAscii(windowsName);
  const std::size_t dot = upper.rfind('.');
  std::string main = dot == std::string::npos ? upper : upper.substr(0, dot);
  std::string ext = dot == std::string::npos ? std::string() : upper.substr(dot + 1);
  const auto clean = [](std::string& text) {
    std::string out;
    for (const char c : text) {
      if (c == ' ' || c == '.') continue;
      out.push_back(isShortNameChar(c) ? c : '_');
    }
    text = out;
  };
  clean(main);
  clean(ext);
  if (main.empty()) main = "FILE";
  if (ext.size() > 3) ext.resize(3);
  const auto build = [&ext](const std::string& base) {
    std::array<std::uint8_t, 11> result;
    result.fill(' ');
    std::copy(base.begin(), base.end(), result.begin());
    std::copy(ext.begin(), ext.end(), result.begin() + 8);
    return result;
  };
  const auto taken = [&items](const std::array<std::uint8_t, 11>& candidate) {
    const std::string text = shortNameText(candidate);
    return std::any_of(items.begin(), items.end(), [&text](const Item& item) { return equalsIgnoreCase(item.shortDisplay, text) || equalsIgnoreCase(item.display, text); });
  };
  if (main.size() <= 8) {
    const auto direct = build(main);
    if (!taken(direct)) return direct;
  }
  for (int number = 1; number < 1000; ++number) {
    const std::string tail = "~" + std::to_string(number);
    const auto candidate = build(main.substr(0, std::min(main.size(), 8 - tail.size())) + tail);
    if (!taken(candidate)) return candidate;
  }
  fail(api::ERROR_EXISTS, "已有同名条目 " + windowsName);
}

// ---------------------------------------------------------------- 接口

std::vector<api::DiskEntry> FatFileSystem::list(const api::EntryPath& directory) {
  std::vector<api::DiskEntry> result;
  for (const Item& item : readDirectory(resolveDirectory(directory))) {
    api::DiskEntry entry;
    entry.name = item.display;
    entry.directory = item.directory();
    if (entry.directory) {
      entry.type = "文件夹";
    } else {
      const std::size_t dot = item.shortDisplay.rfind('.');
      entry.type = dot == std::string::npos ? "文件" : upperAscii(item.shortDisplay.substr(dot + 1));
      entry.size = item.size;
    }
    entry.modified = modifiedText(item.writeDate, item.writeTime);
    entry.locked = (item.attr & ATTR_READ_ONLY) != 0;
    result.push_back(std::move(entry));
  }
  return result;
}

void FatFileSystem::rename(const api::EntryPath& entry, const std::string& name) {
  DirRef parent;
  const Item item = resolveItem(entry, parent);
  const std::array<std::uint8_t, 11> shortName = parseShortName(name);
  ensureNameFree(readDirectory(parent), shortName, &item);
  for (const std::size_t offset : item.lfnOffsets) image_[offset] = ENTRY_FREE;
  std::copy(shortName.begin(), shortName.end(), image_.begin() + static_cast<std::ptrdiff_t>(item.offset));
}

void FatFileSystem::remove(const api::EntryPath& entry) {
  DirRef parent;
  removeItem(resolveItem(entry, parent), 0);
}

void FatFileSystem::move(const api::EntryPath& entry, const api::EntryPath& targetDirectory) {
  DirRef parent;
  const Item item = resolveItem(entry, parent);
  if (item.directory() && targetDirectory.size() >= entry.size()) {
    bool inside = true;
    for (std::size_t i = 0; i < entry.size(); ++i) inside = inside && equalsIgnoreCase(entry[i], targetDirectory[i]);
    if (inside) fail(api::ERROR_MOVE_INTO_SELF, "不能把文件夹移动到它自己里面");
  }
  const DirRef target = resolveDirectory(targetDirectory);
  if (parent.root == target.root && (parent.root || parent.cluster == target.cluster)) return;
  ensureNameFree(readDirectory(target), item.shortName, nullptr);
  const std::size_t count = item.lfnOffsets.size() + 1;
  const std::vector<std::size_t> destination = findFreeSlots(target, count);
  for (std::size_t i = 0; i + 1 < count; ++i) std::copy_n(image_.begin() + static_cast<std::ptrdiff_t>(item.lfnOffsets[i]), ENTRY_SIZE, image_.begin() + static_cast<std::ptrdiff_t>(destination[i]));
  std::copy_n(image_.begin() + static_cast<std::ptrdiff_t>(item.offset), ENTRY_SIZE, image_.begin() + static_cast<std::ptrdiff_t>(destination[count - 1]));
  deleteSlots(item);
  if (item.directory() && item.cluster >= 2) writeLe16(image_.data() + clusterOffset(item.cluster) + ENTRY_SIZE + 26, target.root ? 0 : target.cluster);
}

void FatFileSystem::makeDirectory(const api::EntryPath& directory, const std::string& name) {
  const DirRef parent = resolveDirectory(directory);
  const std::array<std::uint8_t, 11> shortName = parseShortName(name);
  ensureNameFree(readDirectory(parent), shortName, nullptr);
  const std::vector<std::size_t> destination = findFreeSlots(parent, 1);
  const std::uint32_t cluster = allocateChain(1).front();
  const std::size_t base = clusterOffset(cluster);
  std::fill_n(image_.begin() + static_cast<std::ptrdiff_t>(base), geometry_.clusterBytes(), static_cast<std::uint8_t>(0));
  std::array<std::uint8_t, 11> dotName;
  dotName.fill(' ');
  dotName[0] = '.';
  writeEntry(base, dotName, ATTR_DIRECTORY, cluster, 0);
  dotName[1] = '.';
  writeEntry(base + ENTRY_SIZE, dotName, ATTR_DIRECTORY, parent.root ? 0 : parent.cluster, 0);
  writeEntry(destination.front(), shortName, ATTR_DIRECTORY, cluster, 0);
}

api::ExportedFile FatFileSystem::readFile(const api::EntryPath& entry) {
  DirRef parent;
  const Item item = resolveItem(entry, parent);
  if (item.directory()) fail(api::ERROR_UNSUPPORTED, "文件夹不能当作文件读取");
  api::ExportedFile file;
  file.name = item.display;
  file.data.reserve(item.size);
  if (item.size == 0) return file;
  for (const std::uint32_t cluster : chain(item.cluster)) {
    const std::size_t remaining = item.size - file.data.size();
    if (remaining == 0) break;
    const std::size_t take = std::min<std::size_t>(remaining, geometry_.clusterBytes());
    const auto begin = image_.begin() + static_cast<std::ptrdiff_t>(clusterOffset(cluster));
    file.data.insert(file.data.end(), begin, begin + static_cast<std::ptrdiff_t>(take));
  }
  if (file.data.size() != item.size) fail(api::ERROR_CORRUPT, "文件 " + item.display + " 的簇链比它的长度短，文件系统已损坏");
  return file;
}

std::string FatFileSystem::writeFile(const api::EntryPath& directory, const std::string& windowsName, std::span<const std::uint8_t> content) {
  const DirRef parent = resolveDirectory(directory);
  const std::array<std::uint8_t, 11> shortName = importShortName(windowsName, readDirectory(parent));
  const std::size_t clusterBytes = geometry_.clusterBytes();
  const std::size_t needed = (content.size() + clusterBytes - 1) / clusterBytes;
  const std::vector<std::size_t> destination = findFreeSlots(parent, 1);
  const std::vector<std::uint32_t> clusters = allocateChain(needed);
  for (std::size_t i = 0; i < clusters.size(); ++i) {
    const std::size_t take = std::min(clusterBytes, content.size() - i * clusterBytes);
    const std::size_t base = clusterOffset(clusters[i]);
    std::copy_n(content.begin() + static_cast<std::ptrdiff_t>(i * clusterBytes), take, image_.begin() + static_cast<std::ptrdiff_t>(base));
    std::fill(image_.begin() + static_cast<std::ptrdiff_t>(base + take), image_.begin() + static_cast<std::ptrdiff_t>(base + clusterBytes), static_cast<std::uint8_t>(0));
  }
  writeEntry(destination.front(), shortName, ATTR_ARCHIVE, clusters.empty() ? 0 : clusters.front(), static_cast<std::uint32_t>(content.size()));
  return shortNameText(shortName);
}

}  // namespace ember::logic::filemanager
