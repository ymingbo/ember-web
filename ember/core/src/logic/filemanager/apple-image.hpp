/*
 * ------------------------------------------------------------------------
 * 名称：apple-image.hpp
 * 说明：苹果软盘镜像的扇区顺序：把镜像文件里按 DOS 顺序或 ProDOS 顺序排列的扇区，展开成文件系统要的线性字节数组，改完再写回；识别镜像里是 DOS 3.3 还是 ProDOS。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-25
 * 备注：线性数组里第 track 道的第 k 个扇区在 (track × 16 + k) × 256；DOS 3.3 的 k 是 DOS 逻辑扇区号，ProDOS 的 k 是 ProDOS 逻辑扇区号（相邻两个扇区组成一块，块 b 在 b × 512）；
 *       镜像文件的顺序（.dsk、.do 是 DOS 顺序，.po 是 ProDOS 顺序）与文件系统的顺序可以不同（ProDOS 的盘存成 .dsk），换算经物理扇区号；
 *       只处理 35 道与 40 道的 16 扇区盘。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace ember::logic::filemanager {

/** 扇区排列的顺序。 */
enum class AppleOrder { Dos, ProDos };

/** 苹果软盘的扇区大小。 */
inline constexpr std::size_t APPLE_SECTOR_SIZE = 256;
/** 苹果软盘每道的扇区数。 */
inline constexpr std::size_t APPLE_SECTORS = 16;

/** 镜像里文件系统的种类。 */
enum class AppleKind { None, Dos33, ProDos };

/** 识别结果。 */
struct AppleIdentity {
  AppleKind kind{AppleKind::None};
  /** 镜像文件里扇区的排列顺序。 */
  AppleOrder fileOrder{AppleOrder::Dos};
  unsigned tracks{0};
};

/** 镜像大小对应的磁道数：143360 是 35 道，163840 是 40 道，其他为 0。 */
[[nodiscard]] unsigned appleTrackCount(std::size_t imageSize);

/**
 * 识别镜像里的文件系统与扇区顺序：先按扩展名偏好的顺序，再按另一种顺序，各试 ProDOS 与 DOS 3.3；都不是时 kind 为 None。
 * @param extension 小写扩展名，不带点；po 偏好 ProDOS 顺序，其余偏好 DOS 顺序。
 */
[[nodiscard]] AppleIdentity identifyApple(const std::vector<std::uint8_t>& image, std::string_view extension);

/** 把镜像按文件系统的顺序展开成线性字节数组。 */
[[nodiscard]] std::vector<std::uint8_t> appleToLinear(const std::vector<std::uint8_t>& image, AppleOrder fileOrder, AppleOrder fsOrder, unsigned tracks);

/** 把线性字节数组按文件系统的顺序写回镜像（镜像里的位置由文件顺序决定）。 */
void appleFromLinear(std::vector<std::uint8_t>& image, const std::vector<std::uint8_t>& linear, AppleOrder fileOrder, AppleOrder fsOrder, unsigned tracks);

}  // namespace ember::logic::filemanager
