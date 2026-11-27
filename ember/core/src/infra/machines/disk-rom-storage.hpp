/*
 * ------------------------------------------------------------------------
 * 名称：disk-rom-storage.hpp
 * 说明：ROM 文件存取的磁盘实现：已添加的 ROM 保存在用户数据目录的 roms/<机型>/ 下，每个机型最多一个文件。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-15
 * 备注：保存时先写临时文件再改名，改名成功之后才删同目录里的其他文件，所以失败时原有的 ROM 不变；机型标识由调用方按清单检查过，这里仍拒绝带路径分隔符的名字。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <filesystem>

#include "contracts/machines/rom-storage.hpp"

namespace ember::infra::machines {

/** 磁盘上的 ROM 存取。 */
class DiskRomStorage final : public contracts::machines::RomStorage {
 public:
  /** ROM 源文件的大小上限（字节）。 */
  static constexpr std::uintmax_t MAX_FILE_SIZE = 16ULL * 1024 * 1024;

  /** @param root ROM 根目录（用户数据目录下的 rom）。 */
  explicit DiskRomStorage(std::filesystem::path root) : root_(std::move(root)) {}

  [[nodiscard]] std::vector<std::uint8_t> readSource(const std::string& path) const override;
  [[nodiscard]] std::vector<std::string> listSources(const std::string& directory) const override;
  [[nodiscard]] std::optional<contracts::machines::StoredRom> find(const std::string& machine) const override;
  [[nodiscard]] std::vector<std::uint8_t> load(const std::string& machine) const override;
  void store(const std::string& machine, const std::string& fileName, const std::vector<std::uint8_t>& bytes) override;
  void remove(const std::string& machine) override;

 private:
  /** 机型的目录。 */
  [[nodiscard]] std::filesystem::path directoryOf(const std::string& machine) const;
  /** 机型目录里已保存的文件；没有时为空路径。 */
  [[nodiscard]] std::filesystem::path storedFile(const std::string& machine) const;

  std::filesystem::path root_;
};

}  // namespace ember::infra::machines
