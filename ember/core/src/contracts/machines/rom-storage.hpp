/*
 * ------------------------------------------------------------------------
 * 名称：rom-storage.hpp
 * 说明：machines 契约：ROM 文件的存取：读用户选定的源文件，保存、查找、读取与删除各机型已添加的 ROM。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-15
 * 备注：ROM 是用户自己的文件，来源路径由主进程根据用户的选择给出；已添加的 ROM 保存在用户数据目录的 roms/<机型>/ 下，每个机型最多一个文件；
 *       业务代码按机型标识与文件名引用，不拼接磁盘路径；实现在 infra/machines。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ember::contracts::machines {

/** 已保存的 ROM 的描述。 */
struct StoredRom {
  /** 文件名，不含路径。 */
  std::string fileName;
  /** 文件大小（字节）。 */
  std::int64_t size{0};
};

/** ROM 文件的存取。 */
class RomStorage {
 public:
  virtual ~RomStorage() = default;

  /**
   * 读取用户选定的源文件。
   * @param path 文件路径（UTF-8）。
   * @throws shared::CoreError machines.fileNotFound：文件不存在；machines.fileUnreadable：不是普通文件或读取失败；machines.romInvalid：文件大得不可能是 ROM。
   */
  [[nodiscard]] virtual std::vector<std::uint8_t> readSource(const std::string& path) const = 0;

  /**
   * 列出用户选定的目录里可能是 ROM 的文件：扩展名是 .bin（不分大小写）的普通文件，不含子目录，按文件名排序。
   * @param directory 目录路径（UTF-8）。
   * @return 文件的完整路径（UTF-8）。
   * @throws shared::CoreError machines.fileNotFound：目录不存在；machines.fileUnreadable：不是目录或读取失败。
   */
  [[nodiscard]] virtual std::vector<std::string> listSources(const std::string& directory) const = 0;

  /** 机型已保存的 ROM；没有时为空。 */
  [[nodiscard]] virtual std::optional<StoredRom> find(const std::string& machine) const = 0;

  /**
   * 读出机型已保存的 ROM 的内容。
   * @throws shared::CoreError machines.noRom：没有保存；machines.romAccessFailed：读取失败。
   */
  [[nodiscard]] virtual std::vector<std::uint8_t> load(const std::string& machine) const = 0;

  /**
   * 保存机型的 ROM，替换已有的：新文件先写好，成功之后才删旧文件，失败时原有的不变。
   * @param machine 机型标识。
   * @param fileName 文件名，不含路径。
   * @param bytes 文件内容。
   * @throws shared::CoreError machines.romAccessFailed。
   */
  virtual void store(const std::string& machine, const std::string& fileName, const std::vector<std::uint8_t>& bytes) = 0;

  /**
   * 删除机型已保存的 ROM；没有时没有效果。
   * @throws shared::CoreError machines.romAccessFailed。
   */
  virtual void remove(const std::string& machine) = 0;
};

}  // namespace ember::contracts::machines
