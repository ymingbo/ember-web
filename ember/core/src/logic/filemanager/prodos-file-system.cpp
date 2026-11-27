/*
 * ------------------------------------------------------------------------
 * 名称：prodos-file-system.cpp
 * 说明：ProDOS 文件系统的实现。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-25
 * 备注：无。
 * ------------------------------------------------------------------------
 */

#include "logic/filemanager/prodos-file-system.hpp"

#include <algorithm>
#include <cstdio>

#include "contracts/filemanager/filemanager-error.hpp"
#include "logic/filemanager/disk-text.hpp"
#include "shared/errors/core-error.hpp"

namespace ember::logic::filemanager {

namespace api = contracts::filemanager;

namespace {

constexpr std::uint32_t KEY_BLOCK = 2;
constexpr std::size_t BLOCK_SIZE = 512;
constexpr std::size_t ENTRY_SIZE = 0x27;
constexpr unsigned ENTRIES_PER_BLOCK = 13;
constexpr std::size_t FIRST_ENTRY = 4;
constexpr std::uint8_t STORAGE_SEEDLING = 1;
constexpr std::uint8_t STORAGE_SAPLING = 2;
constexpr std::uint8_t STORAGE_TREE = 3;
constexpr std::uint8_t STORAGE_SUBDIRECTORY = 0xD;
constexpr std::uint8_t STORAGE_SUB_HEADER = 0xE;
constexpr std::uint8_t STORAGE_VOLUME_HEADER = 0xF;
constexpr std::size_t MAX_NAME = 15;
constexpr int MAX_DEPTH = 32;
constexpr std::uint8_t TYPE_DIRECTORY = 0x0F;
constexpr std::uint8_t DEFAULT_ACCESS = 0xE3;
constexpr std::size_t INDEX_ENTRIES = 256;
constexpr std::size_t MAX_INDEX_BLOCKS = 128;
constexpr const char* NAME_RULE = "ProDOS 名称是 1 到 15 个字符，以字母开头，只能用字母、数字和点";

[[noreturn]] void fail(std::string_view code, const std::string& message) { throw shared::CoreError(code, message); }

/** 常见文件类型的三字母名称。 */
struct TypeName {
  std::uint8_t type;
  const char* name;
};
constexpr TypeName TYPE_NAMES[] = {{0x00, "NON"}, {0x01, "BAD"}, {0x04, "TXT"}, {0x06, "BIN"}, {0x0F, "DIR"}, {0x19, "ADB"}, {0x1A, "AWP"}, {0x1B, "ASP"}, {0xB0, "SRC"}, {0xB3, "S16"},
                                   {0xB8, "NDA"}, {0xB9, "CDA"}, {0xBA, "TOL"}, {0xBB, "DVR"}, {0xC0, "PNT"}, {0xC1, "PIC"}, {0xE0, "LBR"}, {0xEF, "PAS"}, {0xF0, "CMD"}, {0xFA, "INT"},
                                   {0xFB, "IVR"}, {0xFC, "BAS"}, {0xFD, "VAR"}, {0xFE, "REL"}, {0xFF, "SYS"}};

std::string typeText(std::uint8_t type) {
  for (const TypeName& entry : TYPE_NAMES) {
    if (entry.type == type) return entry.name;
  }
  char text[8];
  std::snprintf(text, sizeof(text), "$%02X", type);
  return text;
}

/** 导出时按类型加的扩展名；没有对应扩展名时为空。 */
const char* typeExtension(std::uint8_t type) {
  switch (type) {
    case 0x04: return ".TXT";
    case 0x06: return ".BIN";
    case 0xFA: return ".INT";
    case 0xFC: return ".BAS";
    case 0xFF: return ".SYS";
    default: return "";
  }
}

/** 目录项里的名称，认小写标记。 */
std::string entryName(const std::uint8_t* entry) {
  const std::size_t length = std::min<std::size_t>(entry[0] & 0x0FU, MAX_NAME);
  std::string name;
  const std::uint32_t caseBits = entry[0x1C] | (static_cast<std::uint32_t>(entry[0x1D]) << 8);
  for (std::size_t i = 0; i < length; ++i) {
    char c = static_cast<char>(entry[1 + i] & 0x7FU);
    if (c < 0x20 || c > 0x7E) c = '?';
    if ((caseBits & 0x8000U) != 0 && c >= 'A' && c <= 'Z' && (caseBits & (0x4000U >> i)) != 0) c = static_cast<char>(c - 'A' + 'a');
    name.push_back(c);
  }
  return name;
}

/** 校验用户给的名称，返回大写形式。 */
std::string normalizeName(const std::string& input) {
  const std::string name = upperAscii(input);
  const bool shapeOk = !name.empty() && name.size() <= MAX_NAME && name.front() >= 'A' && name.front() <= 'Z';
  const bool charsOk = std::all_of(name.begin(), name.end(), [](char c) { return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.'; });
  if (!shapeOk || !charsOk) fail(api::ERROR_INVALID_NAME, NAME_RULE);
  return name;
}

/** 导入时把 Windows 文件名换成合格的 ProDOS 名称。 */
std::string importName(std::string name) {
  name = upperAscii(name);
  for (char& c : name) {
    if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.')) c = '.';
  }
  if (name.empty() || name.front() < 'A' || name.front() > 'Z') name.insert(name.begin(), 'F');
  if (name.size() > MAX_NAME) name.resize(MAX_NAME);
  return name;
}

void storeName(std::uint8_t* entry, const std::string& name) {
  entry[0] = static_cast<std::uint8_t>((entry[0] & 0xF0U) | name.size());
  std::fill(entry + 1, entry + 1 + MAX_NAME, static_cast<std::uint8_t>(0));
  std::copy(name.begin(), name.end(), entry + 1);
  entry[0x1C] = 0;
  entry[0x1D] = 0;
}

std::uint16_t encodeDate(const LocalTime& time) { return static_cast<std::uint16_t>(((time.year % 100) << 9) | (time.month << 5) | time.day); }

/** 写日期（两个字节）与时间（分钟、小时各一个字节）。 */
void storeStamp(std::uint8_t* stamp, const LocalTime& time) {
  writeLe16(stamp, encodeDate(time));
  stamp[2] = static_cast<std::uint8_t>(time.minute);
  stamp[3] = static_cast<std::uint8_t>(time.hour);
}

std::string stampText(const std::uint8_t* stamp) {
  const std::uint32_t date = readLe16(stamp);
  if (date == 0) return {};
  LocalTime time;
  const int year = static_cast<int>((date >> 9) & 0x7FU);
  time.year = year < 40 ? 2000 + year : 1900 + year;
  time.month = static_cast<int>((date >> 5) & 0x0FU);
  time.day = static_cast<int>(date & 0x1FU);
  time.minute = stamp[2];
  time.hour = stamp[3];
  return formatTime(time, false);
}

std::uint32_t pointer(const std::uint8_t* block, std::size_t index) { return static_cast<std::uint32_t>(block[index]) | (static_cast<std::uint32_t>(block[INDEX_ENTRIES + index]) << 8); }

void setPointer(std::uint8_t* block, std::size_t index, std::uint32_t value) {
  block[index] = static_cast<std::uint8_t>(value & 0xFFU);
  block[INDEX_ENTRIES + index] = static_cast<std::uint8_t>(value >> 8);
}

}  // namespace

std::unique_ptr<ProDosFileSystem> ProDosFileSystem::open(std::vector<std::uint8_t>& image, const AppleIdentity& identity, bool writable) {
  return std::unique_ptr<ProDosFileSystem>(new ProDosFileSystem(image, identity, writable));
}

ProDosFileSystem::ProDosFileSystem(std::vector<std::uint8_t>& image, const AppleIdentity& identity, bool writable)
    : image_(image), identity_(identity), writable_(writable), linear_(appleToLinear(image, identity.fileOrder, AppleOrder::ProDos, identity.tracks)) {
  totalBlocks_ = static_cast<std::uint32_t>(std::min<std::size_t>(readLe16(linear_.data() + KEY_BLOCK * BLOCK_SIZE + 0x29), linear_.size() / BLOCK_SIZE));
  traits_.formatId = "prodos";
  traits_.formatName = "ProDOS";
  traits_.writable = writable;
  traits_.subdirectories = true;
}

void ProDosFileSystem::format(std::vector<std::uint8_t>& linear, const std::string& volumeName) {
  std::fill(linear.begin(), linear.end(), static_cast<std::uint8_t>(0));
  const auto total = static_cast<std::uint32_t>(linear.size() / BLOCK_SIZE);
  const std::string name = normalizeName(volumeName);
  const auto block = [&](std::uint32_t number) { return linear.data() + static_cast<std::size_t>(number) * BLOCK_SIZE; };
  for (std::uint32_t number = KEY_BLOCK; number < KEY_BLOCK + 4; ++number) {
    writeLe16(block(number), number > KEY_BLOCK ? number - 1 : 0);
    writeLe16(block(number) + 2, number < KEY_BLOCK + 3 ? number + 1 : 0);
  }
  std::uint8_t* header = block(KEY_BLOCK) + FIRST_ENTRY;
  header[0] = static_cast<std::uint8_t>((STORAGE_VOLUME_HEADER << 4) | name.size());
  std::copy(name.begin(), name.end(), header + 1);
  storeStamp(header + 0x18, currentLocalTime());
  header[0x1E] = 0xC3;
  header[0x1F] = ENTRY_SIZE;
  header[0x20] = ENTRIES_PER_BLOCK;
  writeLe16(header + 0x23, 6);
  writeLe16(header + 0x25, total);
  // 位图：块 7 以后空闲（块 0、1 是引导块，2 到 5 是卷目录，6 是位图）
  std::uint8_t* bitmap = block(6);
  for (std::uint32_t number = 7; number < total; ++number) bitmap[number / 8] = static_cast<std::uint8_t>(bitmap[number / 8] | (0x80U >> (number % 8)));
}

std::uint8_t* ProDosFileSystem::blockAt(std::uint32_t block) {
  if (block >= totalBlocks_) fail(api::ERROR_CORRUPT, "磁盘的块号超出范围，文件系统已损坏");
  return linear_.data() + static_cast<std::size_t>(block) * BLOCK_SIZE;
}

std::uint8_t* ProDosFileSystem::entryAt(const EntryRef& ref) { return blockAt(ref.block) + FIRST_ENTRY + static_cast<std::size_t>(ref.index) * ENTRY_SIZE; }

std::vector<ProDosFileSystem::EntryRef> ProDosFileSystem::directoryEntries(std::uint32_t keyBlock) {
  std::vector<EntryRef> result;
  std::uint32_t block = keyBlock;
  for (std::uint32_t visited = 0; block != 0 && visited < totalBlocks_; ++visited) {
    const std::uint8_t* page = blockAt(block);
    for (unsigned i = 0; i < ENTRIES_PER_BLOCK; ++i) {
      if (block == keyBlock && i == 0) continue;
      result.push_back(EntryRef{block, i});
    }
    block = readLe16(page + 2);
  }
  return result;
}

std::uint32_t ProDosFileSystem::resolveDirectory(const api::EntryPath& path) {
  std::uint32_t key = KEY_BLOCK;
  for (const std::string& name : path) {
    const std::optional<EntryRef> ref = findIn(key, name);
    if (!ref) fail(api::ERROR_ENTRY_NOT_FOUND, "找不到文件夹 " + name);
    const std::uint8_t* entry = entryAt(*ref);
    if ((entry[0] >> 4) != STORAGE_SUBDIRECTORY) fail(api::ERROR_ENTRY_NOT_FOUND, name + " 不是文件夹");
    key = readLe16(entry + 0x11);
  }
  return key;
}

std::optional<ProDosFileSystem::EntryRef> ProDosFileSystem::findIn(std::uint32_t keyBlock, const std::string& name) {
  for (const EntryRef& ref : directoryEntries(keyBlock)) {
    const std::uint8_t* entry = entryAt(ref);
    if ((entry[0] >> 4) == 0) continue;
    if (equalsIgnoreCase(entryName(entry), name)) return ref;
  }
  return std::nullopt;
}

ProDosFileSystem::EntryRef ProDosFileSystem::resolveEntry(const api::EntryPath& path, std::uint32_t& parentKey) {
  if (path.empty()) fail(api::ERROR_ENTRY_NOT_FOUND, "没有指定条目");
  parentKey = resolveDirectory(api::EntryPath(path.begin(), path.end() - 1));
  const std::optional<EntryRef> ref = findIn(parentKey, path.back());
  if (!ref) fail(api::ERROR_ENTRY_NOT_FOUND, "找不到 " + path.back());
  return *ref;
}

ProDosFileSystem::EntryRef ProDosFileSystem::freeEntry(std::uint32_t keyBlock) {
  const std::vector<EntryRef> entries = directoryEntries(keyBlock);
  for (const EntryRef& ref : entries) {
    if ((entryAt(ref)[0] >> 4) == 0) return ref;
  }
  // 目录已满：接一个新块
  const std::uint32_t last = entries.back().block;
  const std::uint32_t added = allocateBlock();
  writeLe16(blockAt(last) + 2, added);
  writeLe16(blockAt(added), last);
  return EntryRef{added, 0};
}

void ProDosFileSystem::adjustFileCount(std::uint32_t keyBlock, int delta) {
  std::uint8_t* count = blockAt(keyBlock) + FIRST_ENTRY + 0x21;
  const int updated = std::max(0, static_cast<int>(readLe16(count)) + delta);
  writeLe16(count, static_cast<std::uint32_t>(updated));
}

bool ProDosFileSystem::blockFree(std::uint32_t block) {
  const std::uint32_t bitmapBlock = readLe16(blockAt(KEY_BLOCK) + FIRST_ENTRY + 0x23);
  const std::size_t byteIndex = block / 8;
  const std::uint8_t* page = blockAt(bitmapBlock + static_cast<std::uint32_t>(byteIndex / BLOCK_SIZE));
  return (page[byteIndex % BLOCK_SIZE] & (0x80U >> (block % 8))) != 0;
}

void ProDosFileSystem::setBlockFree(std::uint32_t block, bool free) {
  const std::uint32_t bitmapBlock = readLe16(blockAt(KEY_BLOCK) + FIRST_ENTRY + 0x23);
  const std::size_t byteIndex = block / 8;
  std::uint8_t& byte = blockAt(bitmapBlock + static_cast<std::uint32_t>(byteIndex / BLOCK_SIZE))[byteIndex % BLOCK_SIZE];
  const auto mask = static_cast<std::uint8_t>(0x80U >> (block % 8));
  byte = free ? static_cast<std::uint8_t>(byte | mask) : static_cast<std::uint8_t>(byte & ~mask);
}

std::uint32_t ProDosFileSystem::allocateBlock() {
  for (std::uint32_t block = 0; block < totalBlocks_; ++block) {
    if (!blockFree(block)) continue;
    setBlockFree(block, false);
    std::fill_n(blockAt(block), BLOCK_SIZE, static_cast<std::uint8_t>(0));
    return block;
  }
  fail(api::ERROR_NO_SPACE, "磁盘空间不足");
}

void ProDosFileSystem::collectBlocks(const std::uint8_t* entry, std::vector<std::uint32_t>& blocks, int depth) {
  if (depth > MAX_DEPTH) fail(api::ERROR_CORRUPT, "文件夹层数过多，文件系统可能已损坏");
  const std::uint8_t storage = entry[0] >> 4;
  const std::uint32_t key = readLe16(entry + 0x11);
  if (storage == STORAGE_SEEDLING) {
    blocks.push_back(key);
  } else if (storage == STORAGE_SAPLING) {
    blocks.push_back(key);
    const std::uint8_t* index = blockAt(key);
    for (std::size_t i = 0; i < INDEX_ENTRIES; ++i) {
      if (const std::uint32_t data = pointer(index, i); data != 0) blocks.push_back(data);
    }
  } else if (storage == STORAGE_TREE) {
    blocks.push_back(key);
    const std::uint8_t* master = blockAt(key);
    for (std::size_t m = 0; m < MAX_INDEX_BLOCKS; ++m) {
      const std::uint32_t indexBlock = pointer(master, m);
      if (indexBlock == 0) continue;
      blocks.push_back(indexBlock);
      const std::uint8_t* index = blockAt(indexBlock);
      for (std::size_t i = 0; i < INDEX_ENTRIES; ++i) {
        if (const std::uint32_t data = pointer(index, i); data != 0) blocks.push_back(data);
      }
    }
  } else if (storage == STORAGE_SUBDIRECTORY) {
    std::uint32_t block = key;
    for (std::uint32_t visited = 0; block != 0 && visited < totalBlocks_; ++visited) {
      blocks.push_back(block);
      block = readLe16(blockAt(block) + 2);
    }
    for (const EntryRef& child : directoryEntries(key)) {
      const std::uint8_t* childEntry = entryAt(child);
      if ((childEntry[0] >> 4) != 0) collectBlocks(childEntry, blocks, depth + 1);
    }
  }
}

std::vector<std::uint32_t> ProDosFileSystem::dataBlocks(const std::uint8_t* entry, std::size_t count) {
  std::vector<std::uint32_t> result(count, 0);
  const std::uint8_t storage = entry[0] >> 4;
  const std::uint32_t key = readLe16(entry + 0x11);
  if (storage == STORAGE_SEEDLING) {
    if (count > 0) result[0] = key;
  } else if (storage == STORAGE_SAPLING) {
    const std::uint8_t* index = blockAt(key);
    for (std::size_t i = 0; i < std::min(count, INDEX_ENTRIES); ++i) result[i] = pointer(index, i);
  } else if (storage == STORAGE_TREE) {
    const std::uint8_t* master = blockAt(key);
    for (std::size_t m = 0; m < MAX_INDEX_BLOCKS && m * INDEX_ENTRIES < count; ++m) {
      const std::uint32_t indexBlock = pointer(master, m);
      if (indexBlock == 0) continue;
      const std::uint8_t* index = blockAt(indexBlock);
      for (std::size_t i = 0; i < INDEX_ENTRIES && m * INDEX_ENTRIES + i < count; ++i) result[m * INDEX_ENTRIES + i] = pointer(index, i);
    }
  }
  return result;
}

void ProDosFileSystem::requireWritable() const {
  if (!writable_) fail(api::ERROR_READ_ONLY, "这个镜像是只读的");
}

void ProDosFileSystem::commit() { appleFromLinear(image_, linear_, identity_.fileOrder, AppleOrder::ProDos, identity_.tracks); }

api::FileSystemSpace ProDosFileSystem::space() {
  std::uint64_t freeBlocks = 0;
  for (std::uint32_t block = 0; block < totalBlocks_; ++block) {
    if (blockFree(block)) ++freeBlocks;
  }
  return api::FileSystemSpace{static_cast<std::uint64_t>(totalBlocks_) * BLOCK_SIZE, freeBlocks * BLOCK_SIZE};
}

std::vector<api::DiskEntry> ProDosFileSystem::list(const api::EntryPath& directory) {
  const std::uint32_t key = resolveDirectory(directory);
  std::vector<api::DiskEntry> result;
  for (const EntryRef& ref : directoryEntries(key)) {
    const std::uint8_t* entry = entryAt(ref);
    const std::uint8_t storage = entry[0] >> 4;
    if (storage == 0) continue;
    api::DiskEntry item;
    item.name = entryName(entry);
    item.directory = storage == STORAGE_SUBDIRECTORY;
    if (item.directory) {
      item.type = "文件夹";
    } else {
      item.type = typeText(entry[0x10]);
      item.size = readLe24(entry + 0x15);
    }
    item.modified = stampText(entry + 0x21);
    if (item.modified.empty()) item.modified = stampText(entry + 0x18);
    item.locked = (entry[0x1E] & 0x02U) == 0;
    result.push_back(std::move(item));
  }
  return result;
}

void ProDosFileSystem::rename(const api::EntryPath& entry, const std::string& name) {
  requireWritable();
  std::uint32_t parentKey = 0;
  const EntryRef ref = resolveEntry(entry, parentKey);
  const std::string normalized = normalizeName(name);
  const std::optional<EntryRef> other = findIn(parentKey, normalized);
  if (other && !(other->block == ref.block && other->index == ref.index)) fail(api::ERROR_EXISTS, "已有同名条目 " + normalized);
  std::uint8_t* found = entryAt(ref);
  storeName(found, normalized);
  if ((found[0] >> 4) == STORAGE_SUBDIRECTORY) storeName(blockAt(readLe16(found + 0x11)) + FIRST_ENTRY, normalized);
  commit();
}

void ProDosFileSystem::remove(const api::EntryPath& entry) {
  requireWritable();
  std::uint32_t parentKey = 0;
  const EntryRef ref = resolveEntry(entry, parentKey);
  std::uint8_t* found = entryAt(ref);
  std::vector<std::uint32_t> blocks;
  collectBlocks(found, blocks, 0);
  for (const std::uint32_t block : blocks) setBlockFree(block, true);
  found[0] = 0;
  adjustFileCount(parentKey, -1);
  commit();
}

void ProDosFileSystem::move(const api::EntryPath& entry, const api::EntryPath& targetDirectory) {
  requireWritable();
  std::uint32_t parentKey = 0;
  const EntryRef ref = resolveEntry(entry, parentKey);
  std::uint8_t* source = entryAt(ref);
  const bool directory = (source[0] >> 4) == STORAGE_SUBDIRECTORY;
  if (directory && targetDirectory.size() >= entry.size()) {
    bool inside = true;
    for (std::size_t i = 0; i < entry.size(); ++i) inside = inside && equalsIgnoreCase(entry[i], targetDirectory[i]);
    if (inside) fail(api::ERROR_MOVE_INTO_SELF, "不能把文件夹移动到它自己里面");
  }
  const std::uint32_t targetKey = resolveDirectory(targetDirectory);
  if (targetKey == parentKey) return;
  if (findIn(targetKey, entry.back())) fail(api::ERROR_EXISTS, "目标文件夹里已有同名条目 " + entry.back());
  const EntryRef destination = freeEntry(targetKey);
  std::uint8_t* target = entryAt(destination);
  std::copy_n(source, ENTRY_SIZE, target);
  writeLe16(target + 0x25, targetKey);
  source = entryAt(ref);
  source[0] = 0;
  adjustFileCount(parentKey, -1);
  adjustFileCount(targetKey, 1);
  if (directory) {
    std::uint8_t* header = blockAt(readLe16(target + 0x11)) + FIRST_ENTRY;
    writeLe16(header + 0x23, destination.block);
    header[0x25] = static_cast<std::uint8_t>(destination.index + 1);
    header[0x26] = ENTRY_SIZE;
  }
  commit();
}

void ProDosFileSystem::makeDirectory(const api::EntryPath& directory, const std::string& name) {
  requireWritable();
  const std::uint32_t parentKey = resolveDirectory(directory);
  const std::string normalized = normalizeName(name);
  if (findIn(parentKey, normalized)) fail(api::ERROR_EXISTS, "已有同名条目 " + normalized);
  const EntryRef slot = freeEntry(parentKey);
  const std::uint32_t key = allocateBlock();
  const LocalTime now = currentLocalTime();
  std::uint8_t* header = blockAt(key) + FIRST_ENTRY;
  header[0] = static_cast<std::uint8_t>((STORAGE_SUB_HEADER << 4) | normalized.size());
  std::copy(normalized.begin(), normalized.end(), header + 1);
  storeStamp(header + 0x18, now);
  header[0x1E] = DEFAULT_ACCESS;
  header[0x1F] = ENTRY_SIZE;
  header[0x20] = ENTRIES_PER_BLOCK;
  writeLe16(header + 0x23, slot.block);
  header[0x25] = static_cast<std::uint8_t>(slot.index + 1);
  header[0x26] = ENTRY_SIZE;
  std::uint8_t* entry = entryAt(slot);
  std::fill_n(entry, ENTRY_SIZE, static_cast<std::uint8_t>(0));
  entry[0] = static_cast<std::uint8_t>((STORAGE_SUBDIRECTORY << 4) | normalized.size());
  std::copy(normalized.begin(), normalized.end(), entry + 1);
  entry[0x10] = TYPE_DIRECTORY;
  writeLe16(entry + 0x11, key);
  writeLe16(entry + 0x13, 1);
  writeLe24(entry + 0x15, BLOCK_SIZE);
  storeStamp(entry + 0x18, now);
  entry[0x1E] = DEFAULT_ACCESS;
  storeStamp(entry + 0x21, now);
  writeLe16(entry + 0x25, parentKey);
  adjustFileCount(parentKey, 1);
  commit();
}

api::ExportedFile ProDosFileSystem::readFile(const api::EntryPath& entry) {
  std::uint32_t parentKey = 0;
  const EntryRef ref = resolveEntry(entry, parentKey);
  const std::uint8_t* found = entryAt(ref);
  if ((found[0] >> 4) == STORAGE_SUBDIRECTORY) fail(api::ERROR_UNSUPPORTED, "文件夹不能当作文件读取");
  const std::uint32_t length = readLe24(found + 0x15);
  const std::size_t count = (static_cast<std::size_t>(length) + BLOCK_SIZE - 1) / BLOCK_SIZE;
  api::ExportedFile file;
  file.data.reserve(length);
  for (const std::uint32_t block : dataBlocks(found, count)) {
    if (block == 0) {
      file.data.insert(file.data.end(), BLOCK_SIZE, 0);
    } else {
      const std::uint8_t* page = blockAt(block);
      file.data.insert(file.data.end(), page, page + BLOCK_SIZE);
    }
  }
  file.data.resize(length);
  file.name = entryName(found) + typeExtension(found[0x10]);
  return file;
}

std::string ProDosFileSystem::writeFile(const api::EntryPath& directory, const std::string& windowsName, std::span<const std::uint8_t> content) {
  requireWritable();
  const std::uint32_t parentKey = resolveDirectory(directory);
  // 按扩展名认类型：认得的扩展名去掉，不认得的当 BIN 并保留在名称里
  const std::size_t dot = windowsName.rfind('.');
  const std::string extension = dot == std::string::npos ? std::string() : upperAscii(windowsName.substr(dot + 1));
  std::uint8_t type = 0x06;
  std::uint32_t aux = 0x2000;
  std::string baseName = windowsName;
  if (extension == "TXT") {
    type = 0x04;
    aux = 0;
    baseName = windowsName.substr(0, dot);
  } else if (extension == "BAS") {
    type = 0xFC;
    aux = 0x0801;
    baseName = windowsName.substr(0, dot);
  } else if (extension == "INT") {
    type = 0xFA;
    aux = 0;
    baseName = windowsName.substr(0, dot);
  } else if (extension == "SYS") {
    type = 0xFF;
    aux = 0x2000;
    baseName = windowsName.substr(0, dot);
  } else if (extension == "BIN") {
    baseName = windowsName.substr(0, dot);
  }
  const std::string name = importName(baseName);
  if (findIn(parentKey, name)) fail(api::ERROR_EXISTS, "已有同名条目 " + name);
  const std::size_t count = (content.size() + BLOCK_SIZE - 1) / BLOCK_SIZE;
  if (count > MAX_INDEX_BLOCKS * INDEX_ENTRIES) fail(api::ERROR_NO_SPACE, "文件太大，ProDOS 的文件最大 16 MB");
  // 空间不够时一块都不分配，不留下半途分配的块
  const std::size_t needed = count <= 1 ? 1 : (count <= INDEX_ENTRIES ? 1 + count : 1 + (count + INDEX_ENTRIES - 1) / INDEX_ENTRIES + count);
  std::size_t available = 0;
  for (std::uint32_t block = 0; block < totalBlocks_ && available < needed; ++block) {
    if (blockFree(block)) ++available;
  }
  if (available < needed) fail(api::ERROR_NO_SPACE, "磁盘空间不足");
  const EntryRef slot = freeEntry(parentKey);
  const auto store = [&](std::uint32_t block, std::size_t blockIndex) {
    std::uint8_t* page = blockAt(block);
    const std::size_t take = std::min(BLOCK_SIZE, content.size() - blockIndex * BLOCK_SIZE);
    std::copy_n(content.begin() + static_cast<std::ptrdiff_t>(blockIndex * BLOCK_SIZE), take, page);
  };
  std::uint8_t storage = STORAGE_SEEDLING;
  std::uint32_t key = 0;
  std::uint32_t used = 1;
  if (count <= 1) {
    key = allocateBlock();
    if (count == 1) store(key, 0);
  } else if (count <= INDEX_ENTRIES) {
    storage = STORAGE_SAPLING;
    key = allocateBlock();
    for (std::size_t i = 0; i < count; ++i) {
      const std::uint32_t block = allocateBlock();
      store(block, i);
      setPointer(blockAt(key), i, block);
    }
    used = static_cast<std::uint32_t>(1 + count);
  } else {
    storage = STORAGE_TREE;
    key = allocateBlock();
    used = 1;
    for (std::size_t m = 0; m * INDEX_ENTRIES < count; ++m) {
      const std::uint32_t indexBlock = allocateBlock();
      ++used;
      setPointer(blockAt(key), m, indexBlock);
      for (std::size_t i = 0; i < INDEX_ENTRIES && m * INDEX_ENTRIES + i < count; ++i) {
        const std::uint32_t block = allocateBlock();
        ++used;
        store(block, m * INDEX_ENTRIES + i);
        setPointer(blockAt(indexBlock), i, block);
      }
    }
  }
  const LocalTime now = currentLocalTime();
  std::uint8_t* entry = entryAt(slot);
  std::fill_n(entry, ENTRY_SIZE, static_cast<std::uint8_t>(0));
  entry[0] = static_cast<std::uint8_t>((storage << 4) | name.size());
  std::copy(name.begin(), name.end(), entry + 1);
  entry[0x10] = type;
  writeLe16(entry + 0x11, key);
  writeLe16(entry + 0x13, used);
  writeLe24(entry + 0x15, static_cast<std::uint32_t>(content.size()));
  storeStamp(entry + 0x18, now);
  entry[0x1E] = DEFAULT_ACCESS;
  writeLe16(entry + 0x1F, aux);
  storeStamp(entry + 0x21, now);
  writeLe16(entry + 0x25, parentKey);
  adjustFileCount(parentKey, 1);
  commit();
  return name;
}

}  // namespace ember::logic::filemanager
