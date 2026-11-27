/*
 * ------------------------------------------------------------------------
 * 名称：cartridge-image.hpp
 * 说明：卡带映像：从 NES 文件解析出的规范化卡带描述，以及 PRG、CHR 数据；插件按描述匹配，卡带按数据创建存储芯片。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * 备注：描述的字段见 docs/03-mapper插件规范.md 第 4.2 节；放在 mappers 契约里，是因为插件匹配只认这些字段，
 *       cartridges 模块解析文件后产生它，mappers 模块消费它，依赖方向只能是 cartridges → mappers。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace ember::contracts::mappers {

/** 文件头里的名称表镜像方式。 */
enum class Mirroring : std::uint8_t {
  /** 水平镜像：$2000 与 $2400 是同一页。 */
  Horizontal,
  /** 垂直镜像：$2000 与 $2800 是同一页。 */
  Vertical,
  /** 四屏：卡带自带名称表显存。 */
  FourScreen
};

/** 镜像方式在脚本与清单里的名称：horizontal、vertical、four。 */
[[nodiscard]] constexpr std::string_view mirroringName(Mirroring mirroring) noexcept {
  switch (mirroring) {
    case Mirroring::Vertical:
      return "vertical";
    case Mirroring::FourScreen:
      return "four";
    case Mirroring::Horizontal:
      break;
  }
  return "horizontal";
}

/** 规范化的卡带描述。 */
struct CartridgeDescription {
  /** mapper 号。 */
  std::uint32_t mapper{0};
  /** 变体：NES 2.0 头取 submapper，旧 iNES 头取第 9 字节右移 1 位。 */
  std::uint32_t variant{0};
  /** PRG 的字节数。 */
  std::size_t prgSize{0};
  /** CHR 的字节数，没有 CHR 时为 0。 */
  std::size_t chrSize{0};
  /** 文件头里的镜像方式。 */
  Mirroring mirroring{Mirroring::Horizontal};
  /** 文件头里的电池标志。 */
  bool battery{false};
  /** 文件头里的训练器标志。 */
  bool trainer{false};
  /** PRG 的 CRC-32。 */
  std::uint32_t crc32Prg{0};
  /** PRG 加 CHR 的 CRC-32。 */
  std::uint32_t crc32All{0};
  /** 是否是 NES 2.0 头；只有 NES 2.0 头才写明下面四项 RAM 的大小。 */
  bool nes20{false};
  /** 文件头写明的 PRG RAM（不带电池）字节数；旧 iNES 头没写明时为空。 */
  std::optional<std::uint32_t> prgRamSize;
  /** 文件头写明的 PRG NVRAM（带电池）字节数；没写明时为空。 */
  std::optional<std::uint32_t> prgNvramSize;
  /** 文件头写明的 CHR RAM 字节数；没写明时为空。 */
  std::optional<std::uint32_t> chrRamSize;
  /** 文件头写明的 CHR NVRAM 字节数；没写明时为空。 */
  std::optional<std::uint32_t> chrNvramSize;
};

/** 卡带映像：描述加数据。 */
struct CartridgeImage {
  CartridgeDescription description;
  std::vector<std::uint8_t> prg;
  std::vector<std::uint8_t> chr;
};

}  // namespace ember::contracts::mappers
