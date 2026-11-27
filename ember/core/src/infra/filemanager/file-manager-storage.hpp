/*
 * ------------------------------------------------------------------------
 * 名称：file-manager-storage.hpp
 * 说明：目录里文件存取的磁盘实现：按文件夹列出目录内容、读取镜像、就地改写、新建镜像文件，文件与文件夹的重命名、移动、复制、删除（进回收站）与新建，以及读取拖入的 Windows 文件与文件夹。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-25
 * 备注：相对路径一律先校验再拼到目录下：拒绝空路径、绝对路径、盘符、反斜杠、“.”“..”与空的层级，再把结果规范化后确认仍在目录里（符号链接指到目录外也拒绝）；
 *       列出不跟随符号链接；读取拖入的内容有总量（64 MB）、层数（16）与个数（5000）的上限。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <memory>

#include "contracts/filemanager/file-manager-storage.hpp"
#include "platform/files/trash-bin.hpp"

namespace ember::infra::filemanager {

/** 磁盘上的目录存取。 */
class FileManagerStorageOnDisk final : public contracts::filemanager::FileManagerStorage {
 public:
  /** @param trash 删除时用的回收站；为空时直接删除（只用在测试里）。 */
  explicit FileManagerStorageOnDisk(std::shared_ptr<platform::TrashBin> trash = nullptr);

  [[nodiscard]] bool isDirectory(const std::string& path) const override;
  [[nodiscard]] std::vector<contracts::filemanager::StoredImageInfo> list(const std::string& root, const std::string& relativeDirectory, bool& available) const override;
  [[nodiscard]] std::optional<contracts::filemanager::StoredImageInfo> stat(const std::string& root, const std::string& relative) const override;
  [[nodiscard]] std::vector<std::uint8_t> read(const std::string& root, const std::string& relative) const override;
  void writeAt(const std::string& root, const std::string& relative, std::size_t offset, std::span<const std::uint8_t> bytes) override;
  [[nodiscard]] std::string create(const std::string& root, const std::string& relativeDirectory, const std::string& baseName, const std::string& extension, const std::vector<std::uint8_t>& bytes) override;
  [[nodiscard]] std::string absolutePath(const std::string& root, const std::string& relative) const override;
  [[nodiscard]] bool samePath(const std::string& a, const std::string& b) const override;
  [[nodiscard]] contracts::filemanager::HostItem readHostItem(const std::string& path) const override;
  [[nodiscard]] bool covers(const std::string& root, const std::string& relative, const std::string& absolute) const override;
  [[nodiscard]] std::string renameItem(const std::string& root, const std::string& relative, const std::string& newName) override;
  [[nodiscard]] std::vector<std::string> moveItems(const std::string& root, const std::vector<std::string>& relatives, const std::string& targetDirectory) override;
  [[nodiscard]] std::vector<std::string> copyItems(const std::string& root, const std::vector<std::string>& relatives, const std::string& targetDirectory) override;
  void removeItems(const std::string& root, const std::vector<std::string>& relatives) override;
  [[nodiscard]] std::string makeFolder(const std::string& root, const std::string& relativeDirectory, const std::string& baseName) override;

 private:
  std::shared_ptr<platform::TrashBin> trash_;
};

}  // namespace ember::infra::filemanager
