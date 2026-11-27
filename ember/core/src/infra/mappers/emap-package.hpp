/*
 * ------------------------------------------------------------------------
 * 名称：emap-package.hpp
 * 说明：mapper 插件包（.emap，zip 格式）的读取与检查：只允许根目录下的 manifest.json、main.js、icon.png，并限制大小。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * 备注：插件包来自外部，属于不可信输入：拒绝目录、符号链接、加密文件、重复文件、其他文件、路径穿越的文件名，
 *       按中央目录声明的解压后大小做限制，解压结果的大小必须与声明一致。规则见 docs/03-mapper插件规范.md 第 3 节。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>

namespace ember::infra::mappers {

/** 整个插件包的大小上限（2 MiB）。 */
inline constexpr std::size_t MAX_PACKAGE_BYTES = 2 * 1024 * 1024;
/** 脚本的大小上限（1 MiB）。 */
inline constexpr std::size_t MAX_SCRIPT_BYTES = 1024 * 1024;
/** 清单的大小上限（256 KiB）。 */
inline constexpr std::size_t MAX_MANIFEST_BYTES = 256 * 1024;
/** 图标的最小边长（像素）。 */
inline constexpr std::uint32_t MIN_ICON_SIZE = 64;

/** 读出的插件包内容。 */
struct EmapPackage {
  /** manifest.json 的内容。 */
  std::string manifestText;
  /** main.js 的内容；包里没有脚本时为空。 */
  std::string scriptText;
  /** 包里是否有 main.js。 */
  bool hasScript{false};
  /** 包里是否有 icon.png。 */
  bool hasIcon{false};
};

/**
 * 读取并检查插件包。
 * @param bytes 包文件的全部内容。
 * @throws shared::CoreError mappers.invalidPackage，说明指出原因。
 */
[[nodiscard]] EmapPackage readEmapPackage(std::span<const std::uint8_t> bytes);

/**
 * 读取未打包的插件目录（开发目录里的一个子目录）：目录根下的 manifest.json、main.js、icon.png，大小与图标的检查同插件包；
 * 其他文件不读，三个文件必须是普通文件（不允许符号链接）。
 * @throws shared::CoreError mappers.invalidPackage，说明指出原因。
 */
[[nodiscard]] EmapPackage readEmapDirectory(const std::filesystem::path& directory);

}  // namespace ember::infra::mappers
