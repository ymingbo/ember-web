/*
 * ------------------------------------------------------------------------
 * 名称：apple-dos-file-system.cpp
 * 说明：Apple DOS 3.3 文件系统的实现。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-25
 * 备注：无。
 * ------------------------------------------------------------------------
 */

#include "logic/filemanager/apple-dos-file-system.hpp"

#include <algorithm>
#include <cstdio>

#include "contracts/filemanager/filemanager-error.hpp"
#include "logic/filemanager/disk-text.hpp"
#include "shared/errors/core-error.hpp"

namespace ember::logic::filemanager {

namespace api = contracts::filemanager;

namespace {

constexpr unsigned VTOC_TRACK = 17;
constexpr std::size_t CATALOG_ENTRY_SIZE = 35;
constexpr std::size_t CATALOG_FIRST_ENTRY = 0x0B;
constexpr std::size_t CATALOG_ENTRIES_PER_SECTOR = 7;
constexpr std::size_t NAME_OFFSET = 3;
constexpr std::size_t NAME_LENGTH = 30;
constexpr std::size_t PAIRS_PER_LIST = 122;
constexpr std::size_t LIST_FIRST_PAIR = 0x0C;
constexpr std::uint8_t ENTRY_DELETED = 0xFF;
constexpr std::uint8_t LOCKED_FLAG = 0x80;
/** 目录链与列表链的最大长度，防止损坏的盘里出现循环。 */
constexpr std::size_t MAX_CHAIN = 1000;
/** 没有扩展名的文件包成 B 型时头里记的装入地址。 */
constexpr std::uint32_t DEFAULT_LOAD_ADDRESS = 0x0800;

[[noreturn]] void fail(std::string_view code, const std::string& message) { throw shared::CoreError(code, message); }

/** 文件类型的显示名称。 */
std::string typeName(std::uint8_t type) {
  switch (type & 0x7FU) {
    case 0x00: return "文本";
    case 0x01: return "整数 BASIC";
    case 0x02: return "Applesoft";
    case 0x04: return "二进制";
    case 0x08: return "S 型";
    case 0x10: return "可重定位";
    case 0x20: return "A 型";
    case 0x40: return "B 型";
    default: {
      char text[8];
      std::snprintf(text, sizeof(text), "$%02X", type & 0x7FU);
      return text;
    }
  }
}

/** 导出时文件名的扩展名。 */
const char* typeExtension(std::uint8_t type) {
  switch (type & 0x7FU) {
    case 0x00: return ".TXT";
    case 0x01: return ".INT";
    case 0x02: return ".BAS";
    case 0x04: return ".BIN";
    default: return ".DAT";
  }
}

/** 目录项里的名称，去掉高位与末尾的空格。 */
std::string entryName(const std::uint8_t* entry) {
  std::string name;
  for (std::size_t i = 0; i < NAME_LENGTH; ++i) {
    const auto c = static_cast<char>(entry[NAME_OFFSET + i] & 0x7FU);
    name.push_back(c >= 0x20 && c < 0x7F ? c : '?');
  }
  while (!name.empty() && name.back() == ' ') name.pop_back();
  return name;
}

/** 校验并规整用户给的名称：1 到 30 个可见 ASCII 字符，不含逗号，转成大写。 */
std::string normalizeName(const std::string& input) {
  const std::string name = upperAscii(input);
  if (name.empty() || name.size() > NAME_LENGTH || !isPrintableAscii(name) || name.find(',') != std::string::npos || name.front() == ' ') {
    fail(api::ERROR_INVALID_NAME, "DOS 3.3 名称是 1 到 30 个字符，只能用 ASCII 可见字符，不能含逗号，不能以空格开头");
  }
  return name;
}

void storeName(std::uint8_t* entry, const std::string& name) {
  for (std::size_t i = 0; i < NAME_LENGTH; ++i) entry[NAME_OFFSET + i] = static_cast<std::uint8_t>(0x80U | static_cast<std::uint8_t>(i < name.size() ? name[i] : ' '));
}

}  // namespace

std::unique_ptr<AppleDosFileSystem> AppleDosFileSystem::open(std::vector<std::uint8_t>& image, const AppleIdentity& identity, bool writable) {
  return std::unique_ptr<AppleDosFileSystem>(new AppleDosFileSystem(image, identity, writable));
}

AppleDosFileSystem::AppleDosFileSystem(std::vector<std::uint8_t>& image, const AppleIdentity& identity, bool writable)
    : image_(image), identity_(identity), writable_(writable), linear_(appleToLinear(image, identity.fileOrder, AppleOrder::Dos, identity.tracks)) {
  traits_.formatId = "appledos33";
  traits_.formatName = "Apple DOS 3.3";
  traits_.writable = writable;
  traits_.subdirectories = false;
}

void AppleDosFileSystem::format(std::vector<std::uint8_t>& linear, unsigned tracks) {
  std::fill(linear.begin(), linear.end(), static_cast<std::uint8_t>(0));
  const auto sector = [&](unsigned track, unsigned index) { return linear.data() + (static_cast<std::size_t>(track) * APPLE_SECTORS + index) * APPLE_SECTOR_SIZE; };
  std::uint8_t* table = sector(VTOC_TRACK, 0);
  table[1] = VTOC_TRACK;
  table[2] = 15;
  table[3] = 3;
  table[6] = 254;
  table[0x27] = PAIRS_PER_LIST;
  table[0x30] = 1;
  table[0x34] = static_cast<std::uint8_t>(tracks);
  table[0x35] = static_cast<std::uint8_t>(APPLE_SECTORS);
  table[0x37] = 1;
  for (unsigned track = 0; track < tracks; ++track) {
    const bool used = track <= 2 || track == VTOC_TRACK;
    table[0x38 + 4 * track] = used ? 0x00 : 0xFF;
    table[0x38 + 4 * track + 1] = used ? 0x00 : 0xFF;
  }
  // 目录扇区从 15 到 1 依次链接，最后一个的下一个是 0
  for (unsigned index = 15; index >= 1; --index) {
    std::uint8_t* catalog = sector(VTOC_TRACK, index);
    catalog[1] = index > 1 ? VTOC_TRACK : 0;
    catalog[2] = index > 1 ? static_cast<std::uint8_t>(index - 1) : 0;
  }
}

std::uint8_t* AppleDosFileSystem::sectorAt(unsigned track, unsigned sector) {
  if (track >= identity_.tracks || sector >= APPLE_SECTORS) fail(api::ERROR_CORRUPT, "磁盘的磁道扇区号超出范围，文件系统已损坏");
  return linear_.data() + (static_cast<std::size_t>(track) * APPLE_SECTORS + sector) * APPLE_SECTOR_SIZE;
}

std::vector<std::uint8_t*> AppleDosFileSystem::catalogEntries() {
  std::vector<std::uint8_t*> entries;
  unsigned track = vtoc()[1];
  unsigned sector = vtoc()[2];
  for (std::size_t visited = 0; track != 0 && visited < MAX_CHAIN; ++visited) {
    std::uint8_t* page = sectorAt(track, sector);
    for (std::size_t i = 0; i < CATALOG_ENTRIES_PER_SECTOR; ++i) entries.push_back(page + CATALOG_FIRST_ENTRY + i * CATALOG_ENTRY_SIZE);
    track = page[1];
    sector = page[2];
  }
  return entries;
}

std::uint8_t* AppleDosFileSystem::findEntry(const api::EntryPath& path) {
  if (path.size() != 1) fail(api::ERROR_ENTRY_NOT_FOUND, "找不到 " + (path.empty() ? std::string("条目") : path.back()));
  for (std::uint8_t* entry : catalogEntries()) {
    if (entry[0] == 0) break;
    if (entry[0] == ENTRY_DELETED) continue;
    if (equalsIgnoreCase(entryName(entry), path.front())) return entry;
  }
  fail(api::ERROR_ENTRY_NOT_FOUND, "找不到 " + path.front());
}

bool AppleDosFileSystem::sectorFree(unsigned track, unsigned sector) {
  const std::uint8_t* bits = vtoc() + 0x38 + 4 * track;
  return (bits[sector >= 8 ? 0 : 1] & (1U << (sector & 7U))) != 0;
}

void AppleDosFileSystem::setSectorFree(unsigned track, unsigned sector, bool free) {
  std::uint8_t& byte = vtoc()[0x38 + 4 * track + (sector >= 8 ? 0 : 1)];
  const auto mask = static_cast<std::uint8_t>(1U << (sector & 7U));
  byte = free ? static_cast<std::uint8_t>(byte | mask) : static_cast<std::uint8_t>(byte & ~mask);
}

void AppleDosFileSystem::collectFile(const std::uint8_t* entry, std::vector<TrackSector>& lists, std::vector<TrackSector>& data) {
  unsigned track = entry[0];
  unsigned sector = entry[1];
  for (std::size_t visited = 0; track != 0 && visited < MAX_CHAIN; ++visited) {
    lists.emplace_back(track, sector);
    const std::uint8_t* list = sectorAt(track, sector);
    const std::size_t base = readLe16(list + 5);
    for (std::size_t i = 0; i < PAIRS_PER_LIST; ++i) {
      const unsigned dataTrack = list[LIST_FIRST_PAIR + 2 * i];
      const unsigned dataSector = list[LIST_FIRST_PAIR + 2 * i + 1];
      if (dataTrack == 0 && dataSector == 0) continue;
      if (data.size() <= base + i) data.resize(base + i + 1, TrackSector{0, 0});
      data[base + i] = TrackSector{dataTrack, dataSector};
    }
    track = list[1];
    sector = list[2];
  }
}

std::vector<AppleDosFileSystem::TrackSector> AppleDosFileSystem::allocateSectors(std::size_t count) {
  std::vector<TrackSector> result;
  // 先用靠近卷目录表的磁道：17 之后向里，再回到 17 之前向外
  std::vector<unsigned> order;
  for (unsigned track = VTOC_TRACK + 1; track < identity_.tracks; ++track) order.push_back(track);
  for (unsigned track = VTOC_TRACK; track-- > 1;) order.push_back(track);
  for (const unsigned track : order) {
    for (unsigned sector = APPLE_SECTORS; sector-- > 0 && result.size() < count;) {
      if (sectorFree(track, sector)) {
        setSectorFree(track, sector, false);
        result.emplace_back(track, sector);
      }
    }
    if (result.size() == count) return result;
  }
  // 空间不够：把已经占下的扇区放回去
  for (const auto& [track, sector] : result) setSectorFree(track, sector, true);
  fail(api::ERROR_NO_SPACE, "磁盘空间不足");
}

void AppleDosFileSystem::requireWritable() const {
  if (!writable_) fail(api::ERROR_READ_ONLY, "这个镜像是只读的");
}

void AppleDosFileSystem::commit() { appleFromLinear(image_, linear_, identity_.fileOrder, AppleOrder::Dos, identity_.tracks); }

api::FileSystemSpace AppleDosFileSystem::space() {
  std::uint64_t freeSectors = 0;
  for (unsigned track = 0; track < identity_.tracks; ++track) {
    for (unsigned sector = 0; sector < APPLE_SECTORS; ++sector) {
      if (sectorFree(track, sector)) ++freeSectors;
    }
  }
  return api::FileSystemSpace{static_cast<std::uint64_t>(identity_.tracks) * APPLE_SECTORS * APPLE_SECTOR_SIZE, freeSectors * APPLE_SECTOR_SIZE};
}

std::vector<api::DiskEntry> AppleDosFileSystem::list(const api::EntryPath& directory) {
  if (!directory.empty()) fail(api::ERROR_ENTRY_NOT_FOUND, "DOS 3.3 磁盘没有文件夹");
  std::vector<api::DiskEntry> result;
  for (const std::uint8_t* entry : catalogEntries()) {
    if (entry[0] == 0) break;
    if (entry[0] == ENTRY_DELETED) continue;
    api::DiskEntry item;
    item.name = entryName(entry);
    item.type = typeName(entry[2]);
    item.size = static_cast<std::int64_t>(readLe16(entry + 0x21)) * static_cast<std::int64_t>(APPLE_SECTOR_SIZE);
    item.sizeIsAllocated = true;
    item.locked = (entry[2] & LOCKED_FLAG) != 0;
    result.push_back(std::move(item));
  }
  return result;
}

void AppleDosFileSystem::rename(const api::EntryPath& entry, const std::string& name) {
  requireWritable();
  std::uint8_t* found = findEntry(entry);
  const std::string normalized = normalizeName(name);
  for (const std::uint8_t* other : catalogEntries()) {
    if (other[0] == 0) break;
    if (other[0] == ENTRY_DELETED || other == found) continue;
    if (equalsIgnoreCase(entryName(other), normalized)) fail(api::ERROR_EXISTS, "已有同名文件 " + normalized);
  }
  storeName(found, normalized);
  commit();
}

void AppleDosFileSystem::remove(const api::EntryPath& entry) {
  requireWritable();
  std::uint8_t* found = findEntry(entry);
  std::vector<TrackSector> lists;
  std::vector<TrackSector> data;
  collectFile(found, lists, data);
  for (const auto& [track, sector] : lists) setSectorFree(track, sector, true);
  for (const auto& [track, sector] : data) {
    if (track != 0) setSectorFree(track, sector, true);
  }
  found[NAME_OFFSET + NAME_LENGTH - 1] = found[0];
  found[0] = ENTRY_DELETED;
  commit();
}

void AppleDosFileSystem::move(const api::EntryPath&, const api::EntryPath&) { fail(api::ERROR_UNSUPPORTED, "DOS 3.3 磁盘没有文件夹，不能移动文件"); }

void AppleDosFileSystem::makeDirectory(const api::EntryPath&, const std::string&) { fail(api::ERROR_UNSUPPORTED, "DOS 3.3 磁盘不能新建文件夹"); }

api::ExportedFile AppleDosFileSystem::readFile(const api::EntryPath& entry) {
  const std::uint8_t* found = findEntry(entry);
  std::vector<TrackSector> lists;
  std::vector<TrackSector> data;
  collectFile(found, lists, data);
  std::vector<std::uint8_t> content;
  content.reserve(data.size() * APPLE_SECTOR_SIZE);
  for (const auto& [track, sector] : data) {
    if (track == 0) {
      content.insert(content.end(), APPLE_SECTOR_SIZE, 0);
      continue;
    }
    const std::uint8_t* page = sectorAt(track, sector);
    content.insert(content.end(), page, page + APPLE_SECTOR_SIZE);
  }
  const std::uint8_t type = found[2] & 0x7FU;
  std::size_t length = content.size();
  if (type == 0x04 && content.size() >= 4) length = std::min<std::size_t>(content.size(), 4U + readLe16(content.data() + 2));
  else if ((type == 0x01 || type == 0x02) && content.size() >= 2) length = std::min<std::size_t>(content.size(), 2U + readLe16(content.data()));
  else if (type == 0x00) {
    while (length > 0 && content[length - 1] == 0) --length;
  }
  content.resize(length);
  api::ExportedFile file;
  file.name = windowsSafeName(entryName(found)) + typeExtension(found[2]);
  file.data = std::move(content);
  return file;
}

std::string AppleDosFileSystem::writeFile(const api::EntryPath& directory, const std::string& windowsName, std::span<const std::uint8_t> content) {
  requireWritable();
  if (!directory.empty()) fail(api::ERROR_ENTRY_NOT_FOUND, "DOS 3.3 磁盘没有文件夹");
  // 按扩展名认类型：认得的扩展名去掉，内容原样；不认得的包成 B 型
  const std::size_t dot = windowsName.rfind('.');
  const std::string extension = dot == std::string::npos ? std::string() : upperAscii(windowsName.substr(dot + 1));
  std::uint8_t type = 0x04;
  std::string baseName = windowsName;
  std::vector<std::uint8_t> payload(content.begin(), content.end());
  if (extension == "TXT" || extension == "BAS" || extension == "INT" || extension == "BIN") {
    type = extension == "TXT" ? 0x00 : extension == "BAS" ? 0x02 : extension == "INT" ? 0x01 : 0x04;
    baseName = windowsName.substr(0, dot);
  } else {
    if (content.size() > 0xFFFFU) fail(api::ERROR_NO_SPACE, "文件太大，DOS 3.3 的 B 型文件最大 64 KB");
    payload.insert(payload.begin(), {static_cast<std::uint8_t>(DEFAULT_LOAD_ADDRESS & 0xFFU), static_cast<std::uint8_t>(DEFAULT_LOAD_ADDRESS >> 8), static_cast<std::uint8_t>(content.size() & 0xFFU),
                                     static_cast<std::uint8_t>(content.size() >> 8)});
  }
  for (char& c : baseName) {
    if (c == ',' || c < 0x20 || c > 0x7E) c = '_';
  }
  baseName = upperAscii(baseName);
  if (baseName.size() > NAME_LENGTH) baseName.resize(NAME_LENGTH);
  while (!baseName.empty() && baseName.front() == ' ') baseName.erase(baseName.begin());
  if (baseName.empty()) baseName = "FILE";
  std::uint8_t* slot = nullptr;
  for (std::uint8_t* candidate : catalogEntries()) {
    if (candidate[0] == 0 || candidate[0] == ENTRY_DELETED) {
      if (slot == nullptr) slot = candidate;
      if (candidate[0] == 0) break;
      continue;
    }
    if (equalsIgnoreCase(entryName(candidate), baseName)) fail(api::ERROR_EXISTS, "已有同名文件 " + baseName);
  }
  if (slot == nullptr) fail(api::ERROR_NO_SPACE, "DOS 3.3 的目录已满");
  const std::size_t dataCount = (payload.size() + APPLE_SECTOR_SIZE - 1) / APPLE_SECTOR_SIZE;
  const std::size_t listCount = std::max<std::size_t>(1, (dataCount + PAIRS_PER_LIST - 1) / PAIRS_PER_LIST);
  const std::vector<TrackSector> sectors = allocateSectors(listCount + dataCount);
  for (std::size_t i = 0; i < dataCount; ++i) {
    std::uint8_t* page = sectorAt(sectors[listCount + i].first, sectors[listCount + i].second);
    std::fill(page, page + APPLE_SECTOR_SIZE, static_cast<std::uint8_t>(0));
    const std::size_t take = std::min(APPLE_SECTOR_SIZE, payload.size() - i * APPLE_SECTOR_SIZE);
    std::copy_n(payload.begin() + static_cast<std::ptrdiff_t>(i * APPLE_SECTOR_SIZE), take, page);
  }
  for (std::size_t j = 0; j < listCount; ++j) {
    std::uint8_t* list = sectorAt(sectors[j].first, sectors[j].second);
    std::fill(list, list + APPLE_SECTOR_SIZE, static_cast<std::uint8_t>(0));
    if (j + 1 < listCount) {
      list[1] = static_cast<std::uint8_t>(sectors[j + 1].first);
      list[2] = static_cast<std::uint8_t>(sectors[j + 1].second);
    }
    writeLe16(list + 5, static_cast<std::uint32_t>(j * PAIRS_PER_LIST));
    for (std::size_t i = j * PAIRS_PER_LIST; i < std::min(dataCount, (j + 1) * PAIRS_PER_LIST); ++i) {
      list[LIST_FIRST_PAIR + 2 * (i - j * PAIRS_PER_LIST)] = static_cast<std::uint8_t>(sectors[listCount + i].first);
      list[LIST_FIRST_PAIR + 2 * (i - j * PAIRS_PER_LIST) + 1] = static_cast<std::uint8_t>(sectors[listCount + i].second);
    }
  }
  std::fill(slot, slot + CATALOG_ENTRY_SIZE, static_cast<std::uint8_t>(0));
  slot[0] = static_cast<std::uint8_t>(sectors[0].first);
  slot[1] = static_cast<std::uint8_t>(sectors[0].second);
  slot[2] = type;
  storeName(slot, baseName);
  writeLe16(slot + 0x21, static_cast<std::uint32_t>(listCount + dataCount));
  commit();
  return baseName;
}

}  // namespace ember::logic::filemanager
