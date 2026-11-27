/*
 * ------------------------------------------------------------------------
 * 名称：fat-file-system.hpp
 * 说明：FAT12 与 FAT16 文件系统：软盘与小容量硬盘镜像的目录（含子目录）、重命名、删除、移动、新建文件夹、读写文件。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-25
 * 备注：列目录时认长文件名（Windows 建的盘有），改动时只写 8.3 名称（SB-DOS 不认长名）：重命名与移动带着长名条目走或删掉，新建的条目只有 8.3 名称；
 *       目录项的日期时间取本地时间；不支持 FAT32（镜像大小上限 16 MB，放不下）。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "contracts/filemanager/disk-file-system.hpp"

namespace ember::logic::filemanager {

/** FAT12 或 FAT16 文件系统。 */
class FatFileSystem final : public contracts::filemanager::DiskFileSystem {
 public:
  /**
   * 按引导扇区里的参数块打开；不是合格的 FAT12 或 FAT16 时返回空。
   * @param image 镜像字节，文件系统直接改它；必须活得比返回的对象久。
   */
  [[nodiscard]] static std::unique_ptr<FatFileSystem> open(std::vector<std::uint8_t>& image);

  [[nodiscard]] const contracts::filemanager::FileSystemTraits& traits() const override { return traits_; }
  [[nodiscard]] contracts::filemanager::FileSystemSpace space() override;
  [[nodiscard]] std::vector<contracts::filemanager::DiskEntry> list(const contracts::filemanager::EntryPath& directory) override;
  void rename(const contracts::filemanager::EntryPath& entry, const std::string& name) override;
  void remove(const contracts::filemanager::EntryPath& entry) override;
  void move(const contracts::filemanager::EntryPath& entry, const contracts::filemanager::EntryPath& targetDirectory) override;
  void makeDirectory(const contracts::filemanager::EntryPath& directory, const std::string& name) override;
  [[nodiscard]] contracts::filemanager::ExportedFile readFile(const contracts::filemanager::EntryPath& entry) override;
  std::string writeFile(const contracts::filemanager::EntryPath& directory, const std::string& windowsName, std::span<const std::uint8_t> content) override;

 private:
  /** 引导扇区里的参数与由它算出的布局。 */
  struct Geometry {
    std::uint32_t bytesPerSector{0};
    std::uint32_t sectorsPerCluster{0};
    std::uint32_t fatCount{0};
    std::uint32_t rootEntries{0};
    std::uint32_t fatStart{0};
    std::uint32_t fatSectors{0};
    std::uint32_t rootStart{0};
    std::uint32_t dataStart{0};
    std::uint32_t clusterCount{0};
    bool fat12{true};
    [[nodiscard]] std::uint32_t clusterBytes() const { return bytesPerSector * sectorsPerCluster; }
  };

  /** 一个目录：根目录（固定区域）或一条簇链。 */
  struct DirRef {
    bool root{true};
    std::uint32_t cluster{0};
  };

  /** 目录里的一个条目：8.3 目录项加它前面的长文件名目录项。 */
  struct Item {
    std::string display;
    std::string shortDisplay;
    std::array<std::uint8_t, 11> shortName{};
    std::uint8_t attr{0};
    std::uint16_t writeTime{0};
    std::uint16_t writeDate{0};
    std::uint32_t cluster{0};
    std::uint32_t size{0};
    std::size_t offset{0};
    std::vector<std::size_t> lfnOffsets;
    [[nodiscard]] bool directory() const { return (attr & 0x10U) != 0; }
  };

  FatFileSystem(std::vector<std::uint8_t>& image, const Geometry& geometry);

  // ---- FAT 表 ----
  [[nodiscard]] std::uint32_t fatGet(std::uint32_t cluster) const;
  void fatSet(std::uint32_t cluster, std::uint32_t value);
  [[nodiscard]] std::uint32_t endOfChain() const { return geometry_.fat12 ? 0xFFFU : 0xFFFFU; }
  [[nodiscard]] bool isEndOfChain(std::uint32_t value) const { return value >= (geometry_.fat12 ? 0xFF8U : 0xFFF8U); }
  [[nodiscard]] std::vector<std::uint32_t> chain(std::uint32_t first) const;
  [[nodiscard]] std::size_t clusterOffset(std::uint32_t cluster) const;
  [[nodiscard]] std::vector<std::uint32_t> allocateChain(std::size_t count);
  void freeChain(std::uint32_t first);

  // ---- 目录 ----
  [[nodiscard]] std::vector<std::size_t> slots(const DirRef& directory) const;
  [[nodiscard]] std::vector<Item> readDirectory(const DirRef& directory) const;
  [[nodiscard]] std::vector<std::size_t> findFreeSlots(const DirRef& directory, std::size_t count);
  [[nodiscard]] DirRef resolveDirectory(const contracts::filemanager::EntryPath& path) const;
  [[nodiscard]] Item resolveItem(const contracts::filemanager::EntryPath& path, DirRef& parent) const;
  [[nodiscard]] static const Item* findItem(const std::vector<Item>& items, const std::string& name);
  void writeEntry(std::size_t offset, const std::array<std::uint8_t, 11>& shortName, std::uint8_t attr, std::uint32_t cluster, std::uint32_t size);
  void deleteSlots(const Item& item);
  void removeItem(const Item& item, int depth);
  void ensureNameFree(const std::vector<Item>& items, const std::array<std::uint8_t, 11>& shortName, const Item* ignore) const;
  [[nodiscard]] std::array<std::uint8_t, 11> importShortName(const std::string& windowsName, const std::vector<Item>& items) const;

  std::vector<std::uint8_t>& image_;
  Geometry geometry_;
  contracts::filemanager::FileSystemTraits traits_;
};

}  // namespace ember::logic::filemanager
