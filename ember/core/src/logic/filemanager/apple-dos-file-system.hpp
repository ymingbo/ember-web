/*
 * ------------------------------------------------------------------------
 * 名称：apple-dos-file-system.hpp
 * 说明：Apple DOS 3.3 文件系统：35 道或 40 道的 16 扇区盘，目录没有子目录；列目录、重命名、删除、读写文件。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-25
 * 备注：文件系统在线性展开的副本上工作，每个修改操作成功后一次写回镜像，中途出错镜像不变；
 *       导出文件时 A、I、B 型按文件头里的长度截断，T 型去掉末尾的 0，导出的文件名带类型对应的扩展名（TXT、BAS、INT、BIN）；
 *       导入时按扩展名认类型：TXT 是 T 型，BAS 是 A 型，INT 是 I 型，BIN 是 B 型，内容原样（A、I、B 型的头要在文件里）；其他扩展名的文件包成 B 型（加 4 字节头，地址 $0800）。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "contracts/filemanager/disk-file-system.hpp"
#include "logic/filemanager/apple-image.hpp"

namespace ember::logic::filemanager {

/** Apple DOS 3.3 文件系统。 */
class AppleDosFileSystem final : public contracts::filemanager::DiskFileSystem {
 public:
  /**
   * 打开已经识别为 DOS 3.3 的镜像。
   * @param image 镜像字节，修改成功后写回它；必须活得比返回的对象久。
   * @param writable 为假时（位流格式解出来的镜像）所有修改操作都拒绝。
   */
  [[nodiscard]] static std::unique_ptr<AppleDosFileSystem> open(std::vector<std::uint8_t>& image, const AppleIdentity& identity, bool writable);

  /** 把线性字节数组（DOS 顺序）格式化成空白的 DOS 3.3 盘：第 0 到 2 道标为已用，第 17 道放卷目录表与目录。 */
  static void format(std::vector<std::uint8_t>& linear, unsigned tracks);

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
  /** 一个磁道扇区位置。 */
  using TrackSector = std::pair<unsigned, unsigned>;

  AppleDosFileSystem(std::vector<std::uint8_t>& image, const AppleIdentity& identity, bool writable);

  [[nodiscard]] std::uint8_t* sectorAt(unsigned track, unsigned sector);
  [[nodiscard]] std::uint8_t* vtoc() { return sectorAt(17, 0); }
  /** 目录里全部目录项的位置（含空的与已删除的）。 */
  [[nodiscard]] std::vector<std::uint8_t*> catalogEntries();
  /** 按名称找到有效的目录项。 */
  [[nodiscard]] std::uint8_t* findEntry(const contracts::filemanager::EntryPath& path);
  [[nodiscard]] bool sectorFree(unsigned track, unsigned sector);
  void setSectorFree(unsigned track, unsigned sector, bool free);
  /** 一个文件占用的全部扇区：T/S 列表扇区与数据扇区，数据扇区按文件内的顺序（没有数据的空洞是 {0, 0}）。 */
  void collectFile(const std::uint8_t* entry, std::vector<TrackSector>& lists, std::vector<TrackSector>& data);
  [[nodiscard]] std::vector<TrackSector> allocateSectors(std::size_t count);
  void requireWritable() const;
  void commit();

  std::vector<std::uint8_t>& image_;
  AppleIdentity identity_;
  bool writable_;
  std::vector<std::uint8_t> linear_;
  contracts::filemanager::FileSystemTraits traits_;
};

}  // namespace ember::logic::filemanager
