/*
 * ------------------------------------------------------------------------
 * 名称：apple-bitstream-decoder.cpp
 * 说明：位流格式苹果软盘镜像的解码器的实现。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-25
 * 备注：无。
 * ------------------------------------------------------------------------
 */

#include "bootstrap/apple-bitstream-decoder.hpp"

#include <algorithm>

#include "logic/hardware/disk2/disk2-codec.hpp"

namespace ember::bootstrap {

namespace {

namespace disk2 = logic::hardware::disk2;

/** WOZ 磁道表里整数磁道对应的四分之一磁道下标倍数。 */
constexpr std::size_t QUARTER_TRACKS_PER_TRACK = 4;
/** 35 道盘与 40 道盘。 */
constexpr unsigned STANDARD_TRACKS = 35;
constexpr unsigned EXTENDED_TRACKS = 40;

}  // namespace

std::optional<std::vector<std::uint8_t>> AppleBitstreamDecoder::decode(std::span<const std::uint8_t> image, std::string_view extension) const {
  unsigned tracks = 0;
  std::vector<std::vector<std::uint8_t>> trackBits;
  if (extension == "nib") {
    if (image.size() == STANDARD_TRACKS * disk2::NIBBLE_TRACK_BYTES) tracks = STANDARD_TRACKS;
    else if (image.size() == EXTENDED_TRACKS * disk2::NIBBLE_TRACK_BYTES) tracks = EXTENDED_TRACKS;
    else return std::nullopt;
    for (unsigned track = 0; track < tracks; ++track) trackBits.push_back(disk2::nibblesToBits(image.subspan(static_cast<std::size_t>(track) * disk2::NIBBLE_TRACK_BYTES, disk2::NIBBLE_TRACK_BYTES)));
  } else if (extension == "woz") {
    const std::optional<disk2::WozImage> woz = disk2::parseWoz(image);
    if (!woz) return std::nullopt;
    tracks = STANDARD_TRACKS;
    trackBits.resize(EXTENDED_TRACKS);
    for (unsigned track = 0; track < EXTENDED_TRACKS; ++track) {
      const std::int16_t index = woz->trackMap[track * QUARTER_TRACKS_PER_TRACK];
      if (index < 0) continue;
      trackBits[track] = disk2::wozTrackBits(image, woz->tracks[static_cast<std::size_t>(index)]);
      if (track >= STANDARD_TRACKS) tracks = EXTENDED_TRACKS;
    }
    trackBits.resize(tracks);
  } else {
    return std::nullopt;
  }
  const disk2::Format format = disk2::appleDosFormat(tracks);
  std::vector<std::uint8_t> result(static_cast<std::size_t>(tracks) * disk2::MAX_SECTORS_PER_TRACK * disk2::SECTOR_SIZE, 0);
  for (unsigned track = 0; track < tracks; ++track) {
    for (const disk2::DecodedSector& sector : disk2::decodeTrack(format, trackBits[track])) {
      if (sector.logical >= disk2::MAX_SECTORS_PER_TRACK) continue;
      std::copy(sector.data.begin(), sector.data.end(), result.begin() + static_cast<std::ptrdiff_t>((static_cast<std::size_t>(track) * disk2::MAX_SECTORS_PER_TRACK + sector.logical) * disk2::SECTOR_SIZE));
    }
  }
  return result;
}

}  // namespace ember::bootstrap
