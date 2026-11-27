/*
 * ------------------------------------------------------------------------
 * 名称：ines-parser.hpp
 * 说明：iNES 1.0 与 NES 2.0 文件的解析：读文件头，得到规范化的卡带描述与 PRG、CHR 数据。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * 备注：变体的取值规则见 docs/03-mapper插件规范.md 第 4.2 节；训练器的内容跳过，只记录标志。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <cstdint>
#include <span>

#include "contracts/mappers/cartridge-image.hpp"

namespace ember::logic::cartridges {

/**
 * 解析 NES 文件。
 * - 魔数为 NES 1A；文件头第 7 字节的位 2-3 为 2，且按 NES 2.0 计算的大小不超过文件长度时，按 NES 2.0 解析，否则按 iNES 1.0。
 * - iNES 1.0 的变体取第 9 字节右移 1 位；NES 2.0 的变体取 submapper。
 * - 文件比头里声明的内容长时，多出的部分被忽略。
 * @param file 完整的文件内容。
 * @throws shared::CoreError cartridges.notInes：魔数不对；cartridges.truncated：文件被截断；
 *         cartridges.unsupportedFormat：UNIF、FDS 或无法处理的大小；cartridges.invalidHeader：PRG 大小为 0。
 */
[[nodiscard]] contracts::mappers::CartridgeImage parseInes(std::span<const std::uint8_t> file);

}  // namespace ember::logic::cartridges
