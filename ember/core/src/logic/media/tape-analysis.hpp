/*
 * ------------------------------------------------------------------------
 * 名称：tape-analysis.hpp
 * 说明：磁带的时间轴分析：读完整盘磁带，得出数据线上各时刻的声音大小，以及连续没有声音的空白段。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-21
 * 备注：数据线取右声道（单声道磁带两个声道相同，所以也是唯一的声道）；
 *       按 10 毫秒一个窗口取最大幅度，窗口的幅度低于满幅的 3% 算静音，连续 0.5 秒以上的静音窗口是一段空白（含磁带开头与结尾的空白）；
 *       声音大小每 0.1 秒一项，取这段时间里各窗口的最大幅度，换成满幅的百分比（有声音时至少为 1）；这些数值是固定的，不是设置项。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

#include "contracts/hardware/tape-deck.hpp"

namespace ember::logic::media {

/** 一盘磁带的分析结果。 */
struct TapeAnalysis {
  /** 总时长（秒），按实际读到的帧数算。 */
  double duration{0};
  /** peaks 里每一项占的时间（秒）。 */
  double step{0};
  /** 每 step 秒一项：数据线的最大幅度占满幅的百分比，0 到 100。 */
  std::vector<std::uint8_t> peaks;
  /** 空白段（起点、终点，秒），按时间排列。 */
  std::vector<std::pair<double, double>> blanks;
};

/** 一个窗口的帧数（10 毫秒）。 */
inline constexpr std::uint64_t TAPE_WINDOW_FRAMES = contracts::hardware::TapeSource::SAMPLE_RATE / 100;
/** 声音大小每一项的窗口数（0.1 秒）。 */
inline constexpr std::uint64_t TAPE_WINDOWS_PER_STEP = 10;
/** 静音的上限：低于它的窗口算静音（满幅 32768 的 3%）。 */
inline constexpr std::int32_t TAPE_SILENCE_LEVEL = 983;
/** 空白段至少包含的静音窗口数（0.5 秒）。 */
inline constexpr std::uint64_t TAPE_BLANK_WINDOWS = 50;

/**
 * 读完整盘磁带并分析；会阻塞到读完为止，所以放在后台线程里调用。
 * @param source 要读的声音来源（从头读；调用后位置在结尾）。
 * @param onProgress 每读一批后回调已读的比例（0 到 1）；来源不知道总时长时不回调。
 * @param cancelled 变为真时尽快返回。
 * @return 分析结果；被取消时为空。
 */
[[nodiscard]] std::optional<TapeAnalysis> analyzeTape(contracts::hardware::TapeSource& source, const std::function<void(double)>& onProgress, const std::atomic<bool>& cancelled);

}  // namespace ember::logic::media
