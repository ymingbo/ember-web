/*
 * ------------------------------------------------------------------------
 * 名称：ines-parser.cpp
 * 说明：iNES 1.0 与 NES 2.0 文件的解析实现。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * 备注：文件头 16 字节：0-3 魔数，4 PRG（16K 为单位），5 CHR（8K 为单位），6 与 7 为标志与 mapper 低位，
 *       8 与 9 在 iNES 1.0 里是 PRG RAM 与制式（这里的变体取第 9 字节右移 1 位，见 docs/03 第 4.2 节），在 NES 2.0 里是 mapper 高位、submapper 与大小高位。
 * ------------------------------------------------------------------------
 */

#include "logic/cartridges/ines-parser.hpp"

#include <algorithm>
#include <optional>
#include <string>

#include "contracts/cartridges/cartridge-error.hpp"
#include "shared/errors/core-error.hpp"
#include "shared/hash/crc32.hpp"

namespace ember::logic::cartridges {

namespace {

namespace cart_error = contracts::cartridges;
using contracts::mappers::CartridgeImage;
using contracts::mappers::Mirroring;

constexpr std::size_t HEADER_SIZE = 16;
constexpr std::uint64_t TRAINER_SIZE = 512;
constexpr std::uint64_t PRG_UNIT = 16384;
constexpr std::uint64_t CHR_UNIT = 8192;
/** NES 2.0 的指数写法里，指数最大取 32（再大的卡带不存在，也避免溢出）。 */
constexpr unsigned MAX_EXPONENT = 32;

/** PRG 与 CHR 的字节数。 */
struct Sizes {
  std::uint64_t prg{0};
  std::uint64_t chr{0};
};

bool startsWith(std::span<const std::uint8_t> file, std::initializer_list<std::uint8_t> prefix) {
  return file.size() >= prefix.size() && std::equal(prefix.begin(), prefix.end(), file.begin());
}

/**
 * NES 2.0 的大小：高位半字节为 F 时用指数写法 2^E × (M×2+1)，否则是 12 位的单位数。
 * @return 无法处理时（指数太大）返回空。
 */
std::optional<std::uint64_t> nes2Size(unsigned low, unsigned highNibble, std::uint64_t unit) {
  if (highNibble == 0x0F) {
    const unsigned exponent = low >> 2;
    if (exponent > MAX_EXPONENT) return std::nullopt;
    return (std::uint64_t{1} << exponent) * ((low & 3U) * 2U + 1U);
  }
  return ((static_cast<std::uint64_t>(highNibble) << 8) | low) * unit;
}

}  // namespace

CartridgeImage parseInes(std::span<const std::uint8_t> file) {
  if (startsWith(file, {'U', 'N', 'I', 'F'})) throw shared::CoreError(cart_error::ERROR_UNSUPPORTED_FORMAT, "这是 UNIF 格式的文件，暂不支持，请先转换为 iNES 格式");
  if (startsWith(file, {'F', 'D', 'S', 0x1A})) throw shared::CoreError(cart_error::ERROR_UNSUPPORTED_FORMAT, "这是 FDS 磁盘镜像，不是卡带文件，暂不支持");
  if (file.size() < HEADER_SIZE || !startsWith(file, {'N', 'E', 'S', 0x1A})) throw shared::CoreError(cart_error::ERROR_NOT_INES, "不是 NES 文件：文件开头应为 \"NES\" 加 1A，请确认选择的是 .nes 卡带文件");

  const std::uint8_t flags6 = file[6];
  const std::uint8_t flags7 = file[7];
  const std::uint64_t trainerSize = (flags6 & 0x04U) != 0 ? TRAINER_SIZE : 0;

  Sizes sizes{static_cast<std::uint64_t>(file[4]) * PRG_UNIT, static_cast<std::uint64_t>(file[5]) * CHR_UNIT};
  bool nes2 = false;
  if ((flags7 & 0x0CU) == 0x08U) {
    // NES 2.0 的标记：按它算出的大小不超过文件长度才可信，否则当作旧头里碰巧写了这两位。
    const auto prg = nes2Size(file[4], file[9] & 0x0FU, PRG_UNIT);
    const auto chr = nes2Size(file[5], file[9] >> 4, CHR_UNIT);
    if (prg && chr && HEADER_SIZE + trainerSize + *prg + *chr <= file.size()) {
      nes2 = true;
      sizes = Sizes{*prg, *chr};
    }
  }

  const std::uint64_t required = HEADER_SIZE + trainerSize + sizes.prg + sizes.chr;
  if (file.size() < required) {
    throw shared::CoreError(cart_error::ERROR_TRUNCATED, "文件被截断：文件头声明的内容共 " + std::to_string(required) + " 字节，文件只有 " + std::to_string(file.size()) + " 字节");
  }
  if (sizes.prg == 0) throw shared::CoreError(cart_error::ERROR_INVALID_HEADER, "文件头不合法：PRG 大小为 0");

  CartridgeImage image;
  auto& description = image.description;
  description.mapper = static_cast<std::uint32_t>((flags6 >> 4) | (flags7 & 0xF0U));
  description.variant = static_cast<std::uint32_t>(file[9] >> 1);
  if (nes2) {
    description.mapper |= static_cast<std::uint32_t>(file[8] & 0x0FU) << 8;
    description.variant = static_cast<std::uint32_t>(file[8] >> 4);
  }
  description.prgSize = static_cast<std::size_t>(sizes.prg);
  description.chrSize = static_cast<std::size_t>(sizes.chr);
  description.mirroring = (flags6 & 0x08U) != 0 ? Mirroring::FourScreen : ((flags6 & 0x01U) != 0 ? Mirroring::Vertical : Mirroring::Horizontal);
  description.battery = (flags6 & 0x02U) != 0;
  description.trainer = trainerSize != 0;
  description.nes20 = nes2;
  if (nes2) {
    // 字节 10、11 的每个半字节是 64 << n 字节，0 表示没有。
    const auto ramSize = [](unsigned nibble) { return nibble == 0 ? 0U : 64U << nibble; };
    description.prgRamSize = ramSize(file[10] & 0x0FU);
    description.prgNvramSize = ramSize(file[10] >> 4);
    description.chrRamSize = ramSize(file[11] & 0x0FU);
    description.chrNvramSize = ramSize(file[11] >> 4);
  }

  const auto prgBegin = file.begin() + static_cast<std::ptrdiff_t>(HEADER_SIZE + trainerSize);
  const auto chrBegin = prgBegin + static_cast<std::ptrdiff_t>(sizes.prg);
  image.prg.assign(prgBegin, chrBegin);
  image.chr.assign(chrBegin, chrBegin + static_cast<std::ptrdiff_t>(sizes.chr));
  description.crc32Prg = shared::crc32(image.prg);
  description.crc32All = shared::crc32Continue(description.crc32Prg, image.chr);
  return image;
}

}  // namespace ember::logic::cartridges
