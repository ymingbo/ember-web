/*
 * ------------------------------------------------------------------------
 * 名称：file-manager-storage.hpp
 * 说明：filemanager 契约：目录里的文件存取：按文件夹列出目录内容、读取镜像、就地改写镜像里的一段字节、新建镜像文件，对镜像文件与文件夹做重命名、移动、复制、删除（进回收站）与新建文件夹，以及读取拖入的 Windows 文件与文件夹。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-25
 * 备注：镜像用“目录 + 相对路径”定位，相对路径用 / 分隔；实现必须拒绝越出目录的路径（含上级目录、盘符、符号链接指到目录外）与不支持的扩展名；
 *       读取拖入的路径只来自用户的拖放，由外壳登记后交给核心；实现在 infra/filemanager；错误经 shared::CoreError 抛出，错误码见 filemanager-error.hpp。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ember::contracts::filemanager {

/** 媒体库里的文件种类：软盘镜像、NES 卡带、音频（录音机能载入的格式）、PNG 图片。 */
enum class FileKind { Floppy, Cartridge, Audio, Picture };

/** 各种类的扩展名（小写，不带点）；.bin 不在管理之内。 */
inline constexpr const char* FLOPPY_EXTENSIONS[] = {"img", "ima", "vfd", "dsk", "flp", "do", "po", "nib", "woz"};
inline constexpr const char* CARTRIDGE_EXTENSIONS[] = {"nes"};
inline constexpr const char* AUDIO_EXTENSIONS[] = {"m4a", "mp3", "wav", "aac", "flac", "ogg", "opus", "mp4", "mka"};
inline constexpr const char* PICTURE_EXTENSIONS[] = {"png"};

/** 扩展名（小写，不带点）对应的文件种类；不在管理之内的为空。 */
[[nodiscard]] inline std::optional<FileKind> kindOfExtension(std::string_view extension) {
  const auto listed = [extension](const auto& extensions) {
    for (const char* item : extensions) {
      if (extension == item) return true;
    }
    return false;
  };
  if (listed(FLOPPY_EXTENSIONS)) return FileKind::Floppy;
  if (listed(CARTRIDGE_EXTENSIONS)) return FileKind::Cartridge;
  if (listed(AUDIO_EXTENSIONS)) return FileKind::Audio;
  if (listed(PICTURE_EXTENSIONS)) return FileKind::Picture;
  return std::nullopt;
}

/** 文件种类的标识，写进 filemanager.tree 的 kind：floppy、cartridge、audio、picture。 */
[[nodiscard]] inline const char* kindId(FileKind kind) {
  switch (kind) {
    case FileKind::Floppy:
      return "floppy";
    case FileKind::Cartridge:
      return "cartridge";
    case FileKind::Audio:
      return "audio";
    case FileKind::Picture:
      return "picture";
  }
  return "floppy";
}

/** 镜像文件的大小上限（字节），与软驱载入的上限一致。 */
inline constexpr std::uintmax_t MAX_IMAGE_SIZE = 16ULL * 1024 * 1024;

/** 列出的一项：文件（不分扩展名，含隐藏文件与符号链接）或文件夹（空文件夹也列出）。 */
struct StoredImageInfo {
  /** 相对目录的路径，用 / 分隔。 */
  std::string path;
  bool directory{false};
  /** 文件大小（字节）；文件夹为 0。 */
  std::uint64_t size{0};
  /** 修改时间的戳，只用来判断文件有没有变化。 */
  std::int64_t stamp{0};
};

/** 拖入的 Windows 文件或文件夹。 */
struct HostItem {
  std::string name;
  bool directory{false};
  /** 文件的内容；文件夹没有。 */
  std::vector<std::uint8_t> data;
  /** 文件夹里的内容，文件夹在前、按名称排序。 */
  std::vector<HostItem> children;
};

/** 目录里的文件存取。 */
class FileManagerStorage {
 public:
  virtual ~FileManagerStorage() = default;

  /** 路径是不是存在的文件夹。 */
  [[nodiscard]] virtual bool isDirectory(const std::string& path) const = 0;

  /**
   * 列出目录里一个文件夹的直接内容（不递归，不读文件内容）：全部子文件夹（含空的）与全部文件（不分扩展名，含隐藏文件；符号链接与联接点不跟随，按文件列出）。
   * @param relativeDirectory 目录里的相对文件夹路径；空串是目录本身。
   * @param available 目录本身不存在或不能读取时置为 false，返回空。
   * @throws shared::CoreError filemanager.invalidPath（relativeDirectory 不合格或不是目录里的文件夹）。
   */
  [[nodiscard]] virtual std::vector<StoredImageInfo> list(const std::string& root, const std::string& relativeDirectory, bool& available) const = 0;

  /** 一个文件的大小与修改时间戳；路径不合格、不存在或不是普通文件时为空。 */
  [[nodiscard]] virtual std::optional<StoredImageInfo> stat(const std::string& root, const std::string& relative) const = 0;

  /**
   * 读取整个镜像。
   * @throws shared::CoreError filemanager.invalidPath、imageNotFound、imageUnreadable（含超过大小上限）。
   */
  [[nodiscard]] virtual std::vector<std::uint8_t> read(const std::string& root, const std::string& relative) const = 0;

  /**
   * 把一段字节写进镜像文件的指定位置，文件的其余部分与大小不变。
   * @throws shared::CoreError filemanager.invalidPath、imageNotFound、saveFailed。
   */
  virtual void writeAt(const std::string& root, const std::string& relative, std::size_t offset, std::span<const std::uint8_t> bytes) = 0;

  /**
   * 在目录的一个文件夹里新建镜像文件，文件名是“基本名.扩展名”，重名时依次加“ (2)”“ (3)”…；先写临时文件再改名。
   * @return 新文件的相对路径。
   * @throws shared::CoreError filemanager.invalidPath、saveFailed。
   */
  [[nodiscard]] virtual std::string create(const std::string& root, const std::string& relativeDirectory, const std::string& baseName, const std::string& extension, const std::vector<std::uint8_t>& bytes) = 0;

  /**
   * 受管理文件的完整路径（软盘镜像、卡带、音频、图片都可以）。
   * @throws shared::CoreError filemanager.invalidPath、imageNotFound。
   */
  [[nodiscard]] virtual std::string absolutePath(const std::string& root, const std::string& relative) const = 0;

  /** 两个路径是不是同一个文件（大小写、分隔符、相对写法不同也算）。 */
  [[nodiscard]] virtual bool samePath(const std::string& a, const std::string& b) const = 0;

  /** 完整路径 absolute 是不是 relative 本身，或在 relative 这个文件夹里面；relative 不存在时为 false。 */
  [[nodiscard]] virtual bool covers(const std::string& root, const std::string& relative, const std::string& absolute) const = 0;

  /**
   * 重命名一个镜像文件或文件夹（不换所在文件夹）；镜像文件的新名称必须保留支持的扩展名；新名称与原名称完全相同时什么都不做。
   * @return 新的相对路径。
   * @throws shared::CoreError filemanager.invalidPath、entryNotFound、invalidName、exists、saveFailed。
   */
  [[nodiscard]] virtual std::string renameItem(const std::string& root, const std::string& relative, const std::string& newName) = 0;

  /**
   * 把镜像文件或文件夹移进另一个文件夹（空串是目录本身），名称不变；已经在目标文件夹里的项跳过。
   * @return 每一项移动之后的相对路径，顺序同 relatives。
   * @throws shared::CoreError filemanager.invalidPath、entryNotFound、exists、moveIntoSelf、saveFailed。
   */
  [[nodiscard]] virtual std::vector<std::string> moveItems(const std::string& root, const std::vector<std::string>& relatives, const std::string& targetDirectory) = 0;

  /**
   * 把镜像文件或文件夹（连同里面的内容）复制进另一个文件夹，重名时名称后加“ - 副本”“ - 副本 (2)”…。
   * @return 每一项复制出来的相对路径，顺序同 relatives。
   * @throws shared::CoreError filemanager.invalidPath、entryNotFound、moveIntoSelf、saveFailed。
   */
  [[nodiscard]] virtual std::vector<std::string> copyItems(const std::string& root, const std::vector<std::string>& relatives, const std::string& targetDirectory) = 0;

  /**
   * 把镜像文件或文件夹移进系统回收站。
   * @throws shared::CoreError filemanager.invalidPath、entryNotFound、saveFailed。
   */
  virtual void removeItems(const std::string& root, const std::vector<std::string>& relatives) = 0;

  /**
   * 在文件夹里新建子文件夹，名称取“基本名”，重名时依次加“ (2)”“ (3)”…。
   * @return 新文件夹的相对路径。
   * @throws shared::CoreError filemanager.invalidPath、saveFailed。
   */
  [[nodiscard]] virtual std::string makeFolder(const std::string& root, const std::string& relativeDirectory, const std::string& baseName) = 0;

  /**
   * 读取拖入的文件或文件夹（文件夹连同里面的全部内容，不跟随符号链接）；总量、层数与个数有上限。
   * @throws shared::CoreError filemanager.invalidPath（找不到、读不了、超过上限）。
   */
  [[nodiscard]] virtual HostItem readHostItem(const std::string& path) const = 0;
};

}  // namespace ember::contracts::filemanager
