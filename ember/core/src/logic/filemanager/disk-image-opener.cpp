/*
 * ------------------------------------------------------------------------
 * 名称：disk-image-opener.cpp
 * 说明：认出磁盘镜像的文件系统并打开，以及空白镜像的生成。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-25
 * 备注：无。
 * ------------------------------------------------------------------------
 */

#include "logic/filemanager/disk-image-opener.hpp"

#include <algorithm>
#include <array>
#include <cstring>

#include "contracts/filemanager/filemanager-error.hpp"
#include "logic/filemanager/apple-dos-file-system.hpp"
#include "logic/filemanager/apple-image.hpp"
#include "logic/filemanager/disk-text.hpp"
#include "logic/filemanager/fat-file-system.hpp"
#include "logic/filemanager/prodos-file-system.hpp"
#include "shared/errors/core-error.hpp"

namespace ember::logic::filemanager {

namespace api = contracts::filemanager;

namespace {

constexpr std::size_t PYRAMID_IMAGE_SIZE = 143360;
constexpr std::size_t FAT_SECTOR_SIZE = 512;

/** 自己持有镜像字节的文件系统：位流格式解码出来的镜像没有别的主人。 */
class OwningFileSystem final : public api::DiskFileSystem {
 public:
  explicit OwningFileSystem(std::vector<std::uint8_t> bytes) : bytes_(std::move(bytes)) {}

  std::vector<std::uint8_t>& bytes() { return bytes_; }
  void attach(std::unique_ptr<api::DiskFileSystem> inner) { inner_ = std::move(inner); }

  [[nodiscard]] const api::FileSystemTraits& traits() const override { return inner_->traits(); }
  [[nodiscard]] api::FileSystemSpace space() override { return inner_->space(); }
  [[nodiscard]] std::vector<api::DiskEntry> list(const api::EntryPath& directory) override { return inner_->list(directory); }
  void rename(const api::EntryPath& entry, const std::string& name) override { inner_->rename(entry, name); }
  void remove(const api::EntryPath& entry) override { inner_->remove(entry); }
  void move(const api::EntryPath& entry, const api::EntryPath& targetDirectory) override { inner_->move(entry, targetDirectory); }
  void makeDirectory(const api::EntryPath& directory, const std::string& name) override { inner_->makeDirectory(directory, name); }
  [[nodiscard]] api::ExportedFile readFile(const api::EntryPath& entry) override { return inner_->readFile(entry); }
  std::string writeFile(const api::EntryPath& directory, const std::string& windowsName, std::span<const std::uint8_t> content) override { return inner_->writeFile(directory, windowsName, content); }

 private:
  // 字节要比文件系统活得久，所以声明在它前面（后销毁）
  std::vector<std::uint8_t> bytes_;
  std::unique_ptr<api::DiskFileSystem> inner_;
};

/** 打开苹果格式的文件系统。 */
std::unique_ptr<api::DiskFileSystem> openApple(std::vector<std::uint8_t>& image, const AppleIdentity& identity, bool writable) {
  if (identity.kind == AppleKind::ProDos) return ProDosFileSystem::open(image, identity, writable);
  return AppleDosFileSystem::open(image, identity, writable);
}

/** 认不出文件系统时的格式说明。 */
void describeUnopenable(const std::vector<std::uint8_t>& image, IdentifiedImage& result) {
  if (!image.empty() && std::all_of(image.begin(), image.end(), [](std::uint8_t byte) { return byte == 0; })) {
    result.formatId = "blank";
    result.formatName = "空白（未格式化）";
  } else if (image.size() == PYRAMID_IMAGE_SIZE) {
    result.formatId = "pyramid";
    result.formatName = "金字塔 DOS（待支持）";
  } else {
    result.formatId = "unknown";
    result.formatName = "未识别";
  }
}

/** 一种 FAT 软盘的参数。 */
struct FatBlankParameters {
  const char* id;
  const char* title;
  std::uint32_t sectors;
  std::uint8_t sectorsPerCluster;
  std::uint16_t rootEntries;
  std::uint8_t media;
  std::uint16_t fatSectors;
  std::uint16_t sectorsPerTrack;
  std::uint16_t heads;
};

constexpr FatBlankParameters FAT_BLANKS[] = {{"fat1440", "3.5 英寸 1.44 MB（FAT12）", 2880, 1, 224, 0xF0, 9, 18, 2},
                                             {"fat720", "3.5 英寸 720 KB（FAT12）", 1440, 2, 112, 0xF9, 3, 9, 2},
                                             {"fat2880", "3.5 英寸 2.88 MB（FAT12）", 5760, 2, 240, 0xF0, 9, 36, 2},
                                             {"fat1200", "5.25 英寸 1.2 MB（FAT12）", 2400, 1, 224, 0xF9, 7, 15, 2},
                                             {"fat360", "5.25 英寸 360 KB（FAT12）", 720, 2, 112, 0xFD, 2, 9, 2}};

std::vector<std::uint8_t> createFatBlank(const FatBlankParameters& parameters) {
  std::vector<std::uint8_t> image(static_cast<std::size_t>(parameters.sectors) * FAT_SECTOR_SIZE, 0);
  std::uint8_t* boot = image.data();
  boot[0] = 0xEB;
  boot[1] = 0x3C;
  boot[2] = 0x90;
  std::memcpy(boot + 3, "EMBER   ", 8);
  writeLe16(boot + 0x0B, FAT_SECTOR_SIZE);
  boot[0x0D] = parameters.sectorsPerCluster;
  writeLe16(boot + 0x0E, 1);
  boot[0x10] = 2;
  writeLe16(boot + 0x11, parameters.rootEntries);
  writeLe16(boot + 0x13, parameters.sectors);
  boot[0x15] = parameters.media;
  writeLe16(boot + 0x16, parameters.fatSectors);
  writeLe16(boot + 0x18, parameters.sectorsPerTrack);
  writeLe16(boot + 0x1A, parameters.heads);
  boot[0x26] = 0x29;
  std::memcpy(boot + 0x2B, "NO NAME    ", 11);
  std::memcpy(boot + 0x36, "FAT12   ", 8);
  // 引导代码：关中断后停机，空白盘不能启动
  boot[0x3E] = 0xFA;
  boot[0x3F] = 0xF4;
  boot[0x40] = 0xEB;
  boot[0x41] = 0xFD;
  boot[510] = 0x55;
  boot[511] = 0xAA;
  for (std::size_t copy = 0; copy < 2; ++copy) {
    std::uint8_t* fat = image.data() + (1 + copy * parameters.fatSectors) * FAT_SECTOR_SIZE;
    fat[0] = parameters.media;
    fat[1] = 0xFF;
    fat[2] = 0xFF;
  }
  return image;
}

}  // namespace

IdentifiedImage identifyImage(std::vector<std::uint8_t>& image, std::string_view extension, const api::AppleBitstreamDecoder* decoder) {
  IdentifiedImage result;
  if (extension == "nib" || extension == "woz") {
    if (decoder != nullptr) {
      if (std::optional<std::vector<std::uint8_t>> decoded = decoder->decode(image, extension)) {
        const AppleIdentity identity = identifyApple(*decoded, "dsk");
        if (identity.kind != AppleKind::None) {
          auto owner = std::make_unique<OwningFileSystem>(std::move(*decoded));
          std::unique_ptr<api::DiskFileSystem> inner = openApple(owner->bytes(), identity, false);
          result.formatId = inner->traits().formatId;
          result.formatName = inner->traits().formatName;
          owner->attach(std::move(inner));
          result.fileSystem = std::move(owner);
          return result;
        }
      }
    }
    result.formatId = "unknown";
    result.formatName = "未识别";
    return result;
  }
  const AppleIdentity apple = identifyApple(image, extension);
  if (apple.kind != AppleKind::None) {
    result.fileSystem = openApple(image, apple, true);
  } else if (std::unique_ptr<FatFileSystem> fat = FatFileSystem::open(image)) {
    result.fileSystem = std::move(fat);
  }
  if (result.fileSystem == nullptr) {
    describeUnopenable(image, result);
    return result;
  }
  result.formatId = result.fileSystem->traits().formatId;
  result.formatName = result.fileSystem->traits().formatName;
  return result;
}

const std::vector<BlankImageKind>& blankImageKinds() {
  static const std::vector<BlankImageKind> kinds = [] {
    std::vector<BlankImageKind> result;
    for (const FatBlankParameters& parameters : FAT_BLANKS) result.push_back(BlankImageKind{parameters.id, parameters.title, "img"});
    result.push_back(BlankImageKind{"appledos33", "5.25 英寸 140 KB（Apple DOS 3.3）", "dsk"});
    result.push_back(BlankImageKind{"prodos", "5.25 英寸 140 KB（ProDOS）", "po"});
    result.push_back(BlankImageKind{"pyramid", "5.25 英寸 140 KB（金字塔 PEC-9588，空白，要在机器里 INIT）", "dsk"});
    return result;
  }();
  return kinds;
}

std::vector<std::uint8_t> createBlankImage(const std::string& kindId) {
  for (const FatBlankParameters& parameters : FAT_BLANKS) {
    if (kindId == parameters.id) return createFatBlank(parameters);
  }
  constexpr unsigned tracks = 35;
  std::vector<std::uint8_t> image(PYRAMID_IMAGE_SIZE, 0);
  if (kindId == "pyramid") return image;
  std::vector<std::uint8_t> linear(PYRAMID_IMAGE_SIZE, 0);
  if (kindId == "appledos33") {
    AppleDosFileSystem::format(linear, tracks);
    appleFromLinear(image, linear, AppleOrder::Dos, AppleOrder::Dos, tracks);
    return image;
  }
  if (kindId == "prodos") {
    ProDosFileSystem::format(linear, "BLANK");
    appleFromLinear(image, linear, AppleOrder::ProDos, AppleOrder::ProDos, tracks);
    return image;
  }
  throw shared::CoreError(api::ERROR_UNSUPPORTED, "没有这种空白镜像：" + kindId);
}

}  // namespace ember::logic::filemanager
