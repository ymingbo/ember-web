/*
 * ------------------------------------------------------------------------
 * 名称：prodos-file-system.hpp
 * 说明：ProDOS 文件系统：35 道或 40 道的 16 扇区盘（块 512 字节），有子目录；列目录、重命名、删除、移动、新建文件夹、读写文件。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-25
 * 备注：文件系统在按块线性展开的副本上工作，每个修改操作成功后一次写回镜像，中途出错镜像不变；
 *       名称读出时认小写标记（ProDOS 2.x 与 GS/OS），写入的名称都是大写；
 *       导出时文件名按类型带扩展名（TXT、BIN、BAS、SYS、INT），导入时按扩展名认类型（认不得的当 BIN）。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "contracts/filemanager/disk-file-system.hpp"
#include "logic/filemanager/apple-image.hpp"

namespace ember::logic::filemanager {

/** ProDOS 文件系统。 */
class ProDosFileSystem final : public contracts::filemanager::DiskFileSystem {
 public:
  /**
   * 打开已经识别为 ProDOS 的镜像。
   * @param image 镜像字节，修改成功后写回它；必须活得比返回的对象久。
   * @param writable 为假时（位流格式解出来的镜像）所有修改操作都拒绝。
   */
  [[nodiscard]] static std::unique_ptr<ProDosFileSystem> open(std::vector<std::uint8_t>& image, const AppleIdentity& identity, bool writable);

  /** 把线性字节数组（ProDOS 顺序）格式化成空白的 ProDOS 盘：块 2 到 5 是卷目录，块 6 是位图。 */
  static void format(std::vector<std::uint8_t>& linear, const std::string& volumeName);

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
  /** 目录块里的一个目录项位置。 */
  struct EntryRef {
    std::uint32_t block{0};
    unsigned index{0};
  };

  ProDosFileSystem(std::vector<std::uint8_t>& image, const AppleIdentity& identity, bool writable);

  [[nodiscard]] std::uint8_t* blockAt(std::uint32_t block);
  [[nodiscard]] std::uint8_t* entryAt(const EntryRef& ref);
  /** 目录（由它的关键块给出）里全部目录项的位置，含空的；不含目录头。 */
  [[nodiscard]] std::vector<EntryRef> directoryEntries(std::uint32_t keyBlock);
  [[nodiscard]] std::uint32_t resolveDirectory(const contracts::filemanager::EntryPath& path);
  [[nodiscard]] std::optional<EntryRef> findIn(std::uint32_t keyBlock, const std::string& name);
  [[nodiscard]] EntryRef resolveEntry(const contracts::filemanager::EntryPath& path, std::uint32_t& parentKey);
  [[nodiscard]] EntryRef freeEntry(std::uint32_t keyBlock);
  void adjustFileCount(std::uint32_t keyBlock, int delta);
  [[nodiscard]] bool blockFree(std::uint32_t block);
  void setBlockFree(std::uint32_t block, bool free);
  [[nodiscard]] std::uint32_t allocateBlock();
  /** 文件或文件夹占用的全部块（关键块、索引块、数据块；目录的整条块链）。 */
  void collectBlocks(const std::uint8_t* entry, std::vector<std::uint32_t>& blocks, int depth);
  /** 文件第 0 到 count-1 个数据块的块号（空洞是 0）。 */
  [[nodiscard]] std::vector<std::uint32_t> dataBlocks(const std::uint8_t* entry, std::size_t count);
  void removeEntry(const EntryRef& ref, std::uint32_t parentKey, int depth);
  void requireWritable() const;
  void commit();

  std::vector<std::uint8_t>& image_;
  AppleIdentity identity_;
  bool writable_;
  std::vector<std::uint8_t> linear_;
  std::uint32_t totalBlocks_{0};
  contracts::filemanager::FileSystemTraits traits_;
};

}  // namespace ember::logic::filemanager
