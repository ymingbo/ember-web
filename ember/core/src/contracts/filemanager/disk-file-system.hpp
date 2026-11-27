/*
 * ------------------------------------------------------------------------
 * 名称：disk-file-system.hpp
 * 说明：filemanager 契约：磁盘镜像里的文件系统。把一个镜像（内存里的字节数组）当作文件系统，列目录、重命名、删除、移动、新建文件夹、读写文件；FAT、Apple DOS 3.3、ProDOS 各有实现。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-25
 * 备注：实现直接改它持有引用的字节数组，不负责把改动写回文件或软驱，由管理模块比较改动前后的字节后统一写回；
 *       条目用名称路径表示（从镜像根目录开始逐级的名称，不含路径分隔符），名称比较不分大小写；
 *       不支持的操作（如 DOS 3.3 没有子目录）抛出 filemanager.unsupported，说明简短、面向用户；错误码见 filemanager-error.hpp；
 *       位流格式（.nib、.woz）经 AppleBitstreamDecoder 解成扇区镜像后只读打开，实现在硬件模块，由组装根注入，业务逻辑不引用硬件实现。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ember::contracts::filemanager {

/** 镜像里的条目路径：从根目录开始逐级的名称；空表示根目录。 */
using EntryPath = std::vector<std::string>;

/** 镜像里的一个条目（文件或文件夹）。 */
struct DiskEntry {
  /** 显示的完整名称（UTF-8）。 */
  std::string name;
  bool directory{false};
  /** 类型文字：FAT 是扩展名、DOS 3.3 与 ProDOS 是类型名，文件夹是“文件夹”。 */
  std::string type;
  /** 大小（字节）；文件夹为 0。 */
  std::int64_t size{0};
  /** 大小是占用的空间（DOS 3.3 只知道扇区数）而不是准确的字节数。 */
  bool sizeIsAllocated{false};
  /** 修改时间，形如 2026-11-25 14:30；文件系统没有时间时为空。 */
  std::string modified;
  /** 只读或锁定：只作显示，不阻止本管理窗口的操作。 */
  bool locked{false};
};

/** 一个文件系统的特性。 */
struct FileSystemTraits {
  /** 格式标识：fat12、fat16、appledos33、prodos。 */
  std::string formatId;
  /** 界面显示的格式名称，如 FAT12、Apple DOS 3.3、ProDOS。 */
  std::string formatName;
  /** 能否修改；只读的镜像（位流格式）为 false。 */
  bool writable{true};
  /** 有没有子目录：没有时不能新建文件夹，也不能移动文件。 */
  bool subdirectories{true};
};

/** 文件系统的容量与可用空间（字节）：容量是能存放文件内容的全部空间（FAT 按簇、DOS 3.3 按扇区、ProDOS 按块）。 */
struct FileSystemSpace {
  std::uint64_t capacity{0};
  std::uint64_t free{0};
};

/** 导出的文件：Windows 上的建议文件名与内容。 */
struct ExportedFile {
  std::string name;
  std::vector<std::uint8_t> data;
};

/** 磁盘镜像里的文件系统。 */
class DiskFileSystem {
 public:
  virtual ~DiskFileSystem() = default;

  [[nodiscard]] virtual const FileSystemTraits& traits() const = 0;

  /** 容量与可用空间。@throws shared::CoreError filemanager.corrupt。 */
  [[nodiscard]] virtual FileSystemSpace space() = 0;

  /**
   * 列出一个目录的内容，顺序与磁盘上的目录一致（文件夹不提前）。
   * @throws shared::CoreError filemanager.entryNotFound、filemanager.corrupt。
   */
  [[nodiscard]] virtual std::vector<DiskEntry> list(const EntryPath& directory) = 0;

  /**
   * 重命名条目（不改所在目录）。
   * @throws shared::CoreError filemanager.readOnly、entryNotFound、invalidName、exists、corrupt。
   */
  virtual void rename(const EntryPath& entry, const std::string& name) = 0;

  /**
   * 删除条目；文件夹连同里面的全部内容。
   * @throws shared::CoreError filemanager.readOnly、entryNotFound、corrupt。
   */
  virtual void remove(const EntryPath& entry) = 0;

  /**
   * 把条目移到另一个目录，名称不变。
   * @throws shared::CoreError filemanager.readOnly、unsupported（没有子目录）、entryNotFound、exists、moveIntoSelf、noSpace、corrupt。
   */
  virtual void move(const EntryPath& entry, const EntryPath& targetDirectory) = 0;

  /**
   * 在目录里新建文件夹。
   * @throws shared::CoreError filemanager.readOnly、unsupported、entryNotFound、invalidName、exists、noSpace、corrupt。
   */
  virtual void makeDirectory(const EntryPath& directory, const std::string& name) = 0;

  /**
   * 读出文件的原样内容（导出到 Windows）。
   * @throws shared::CoreError filemanager.entryNotFound、corrupt。
   */
  [[nodiscard]] virtual ExportedFile readFile(const EntryPath& entry) = 0;

  /**
   * 把 Windows 上的一个文件写进目录（导入）；名称与类型按文件系统的规则由 windowsName 换算（如 FAT 换成 8.3 名称）。
   * @return 写进镜像的名称。
   * @throws shared::CoreError filemanager.readOnly、entryNotFound、invalidName、exists、noSpace、corrupt。
   */
  virtual std::string writeFile(const EntryPath& directory, const std::string& windowsName, std::span<const std::uint8_t> content) = 0;
};

/** 位流格式镜像（.nib、.woz）的解码：解成按 DOS 顺序排列扇区的镜像，供只读查看。 */
class AppleBitstreamDecoder {
 public:
  virtual ~AppleBitstreamDecoder() = default;

  /**
   * @param image 镜像文件的内容。
   * @param extension 小写扩展名，不带点：nib 或 woz。
   * @return 每道 16 个扇区、35 或 40 道的 DOS 顺序镜像（磁道上解不出的扇区填 0）；解不出来时为空。
   */
  [[nodiscard]] virtual std::optional<std::vector<std::uint8_t>> decode(std::span<const std::uint8_t> image, std::string_view extension) const = 0;
};

}  // namespace ember::contracts::filemanager
