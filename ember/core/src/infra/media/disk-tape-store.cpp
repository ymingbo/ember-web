/*
 * ------------------------------------------------------------------------
 * 名称：disk-tape-store.cpp
 * 说明：磁带文件存取的磁盘实现：用静态链接的 FFmpeg 库解码出声音来源，以及 WAV 录音文件。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-15
 * ------------------------------------------------------------------------
 */

#include "infra/media/disk-tape-store.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <system_error>
#include <utility>
#include <vector>

#include "contracts/media/media-error.hpp"
#include "shared/errors/core-error.hpp"

// FFmpeg 是 C 库；它的头文件在 /W4 下有警告，这里只在包含时压住。
#ifdef _MSC_VER
#pragma warning(push, 0)
#endif
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/log.h>
#include <libswresample/swresample.h>
}
#ifdef _MSC_VER
#pragma warning(pop)
#endif

namespace ember::infra::media {

namespace api = contracts::media;
namespace hw = contracts::hardware;
namespace fs = std::filesystem;

namespace {

fs::path toPath(const std::string& text) { return fs::path(std::u8string(text.begin(), text.end())); }

/** 路径转 UTF-8 文本。 */
std::string toUtf8(const fs::path& path) {
  const auto text = path.u8string();
  return std::string(text.begin(), text.end());
}

void writeLe32(std::uint8_t* target, std::uint32_t value) {
  for (unsigned index = 0; index < 4; ++index) target[index] = static_cast<std::uint8_t>(value >> (index * 8));
}

/** 16 位 PCM 的 WAV 文件头（44 字节），数据长度先占位，写完后用 patchWavSizes 补上。 */
std::array<std::uint8_t, 44> wavHeader(unsigned sampleRate, unsigned channels) {
  std::array<std::uint8_t, 44> header{};
  std::memcpy(header.data(), "RIFF", 4);
  std::memcpy(header.data() + 8, "WAVEfmt ", 8);
  writeLe32(header.data() + 16, 16);
  header[20] = 1;  // PCM
  header[22] = static_cast<std::uint8_t>(channels);
  writeLe32(header.data() + 24, sampleRate);
  writeLe32(header.data() + 28, sampleRate * channels * 2);
  header[32] = static_cast<std::uint8_t>(channels * 2);  // 每帧字节数
  header[34] = 16;                                       // 位数
  std::memcpy(header.data() + 36, "data", 4);
  return header;
}

/** 把文件头里的两个长度补成实际写入的数据字节数。 */
void patchWavSizes(std::ofstream& stream, std::uint64_t dataBytes) {
  std::uint8_t size[4];
  writeLe32(size, static_cast<std::uint32_t>(36 + dataBytes));
  stream.seekp(4);
  stream.write(reinterpret_cast<const char*>(size), 4);
  writeLe32(size, static_cast<std::uint32_t>(dataBytes));
  stream.seekp(40);
  stream.write(reinterpret_cast<const char*>(size), 4);
}

/** 16 位采样按小端写入文件。 */
void writeSamples(std::ofstream& stream, const std::int16_t* samples, std::size_t count) {
  std::vector<std::uint8_t> bytes(count * 2);
  for (std::size_t index = 0; index < count; ++index) {
    const auto value = static_cast<std::uint16_t>(samples[index]);
    bytes[index * 2] = static_cast<std::uint8_t>(value & 0xFF);
    bytes[index * 2 + 1] = static_cast<std::uint8_t>(value >> 8);
  }
  stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

/** 解码失败的原因。 */
class DecodeFailure : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

/** FFmpeg 错误码对应的说明。 */
std::string describe(int code) {
  char buffer[AV_ERROR_MAX_STRING_SIZE] = {};
  av_strerror(code, buffer, sizeof(buffer));
  return buffer;
}

/** 已打开的声音文件：解封装、解码与重采样成 96 kHz、16 位、立体声；成员按创建顺序的反序释放。 */
class AudioInput {
 public:
  /** @throws DecodeFailure 打不开、没有声音轨或无法解码。 */
  explicit AudioInput(const std::string& path) {
    try {
      open(path);
    } catch (...) {
      release();
      throw;
    }
  }
  ~AudioInput() { release(); }
  AudioInput(const AudioInput&) = delete;
  AudioInput& operator=(const AudioInput&) = delete;

  /** 文件的时长（秒）；文件里没有写时为 0。 */
  [[nodiscard]] double duration() const { return format_->duration == AV_NOPTS_VALUE ? 0.0 : static_cast<double>(format_->duration) / AV_TIME_BASE; }
  /** 不是单声道。 */
  [[nodiscard]] bool stereo() const { return codec_->ch_layout.nb_channels != 1; }

  /**
   * 跳到 seconds 之前最近的可解码位置，丢掉解码器与重采样器里的旧内容；只在刚打开、还没有解码时调用。
   * 成功后，下一次 decodeMore 解出的第一帧的起点用 decodedStart 读出。
   * @returns 文件不支持跳转时为 false，仍从头解码。
   */
  bool seekTo(double seconds) {
    const AVStream* stream = format_->streams[streamIndex_];
    std::int64_t target = av_rescale_q(static_cast<std::int64_t>(seconds * AV_TIME_BASE), AV_TIME_BASE_Q, stream->time_base);
    if (stream->start_time != AV_NOPTS_VALUE) target += stream->start_time;
    if (av_seek_frame(format_, streamIndex_, target, AVSEEK_FLAG_BACKWARD) < 0) return false;
    avcodec_flush_buffers(codec_);
    if (swr_init(resampler_) < 0) throw DecodeFailure("无法重置重采样器");
    finished_ = false;
    startPending_ = true;
    startKnown_ = false;
    return true;
  }

  /** 跳转后第一帧的起点（秒）；文件没有给出时间戳时 known 为 false。 */
  [[nodiscard]] double decodedStart(bool& known) const {
    known = startKnown_;
    return startSeconds_;
  }

  /**
   * 追加解码出的采样（左右交错）到 samples。
   * @returns 文件还有数据时为 true；已经解完为 false。
   * @throws DecodeFailure 解码中途出错。
   */
  bool decodeMore(std::vector<std::int16_t>& samples) {
    while (!finished_) {
      int code = avcodec_receive_frame(codec_, frame_);
      if (code == 0) {
        convert(frame_, samples);
        av_frame_unref(frame_);
        return true;
      }
      if (code == AVERROR_EOF) {
        // 解码器已吐完，把重采样器里剩下的取出来。
        convert(nullptr, samples);
        finished_ = true;
        return !samples.empty();
      }
      if (code != AVERROR(EAGAIN)) throw DecodeFailure(describe(code));
      feed();
    }
    return false;
  }

 private:
  void open(const std::string& path) {
    int code = avformat_open_input(&format_, path.c_str(), nullptr, nullptr);
    if (code < 0) throw DecodeFailure(describe(code));
    format_->flags |= AVFMT_FLAG_BITEXACT;
    code = avformat_find_stream_info(format_, nullptr);
    if (code < 0) throw DecodeFailure(describe(code));
    const AVCodec* decoder = nullptr;
    streamIndex_ = av_find_best_stream(format_, AVMEDIA_TYPE_AUDIO, -1, -1, &decoder, 0);
    if (streamIndex_ < 0 || decoder == nullptr) throw DecodeFailure("没有声音轨");
    codec_ = avcodec_alloc_context3(decoder);
    if (codec_ == nullptr) throw DecodeFailure("无法创建解码器");
    code = avcodec_parameters_to_context(codec_, format_->streams[streamIndex_]->codecpar);
    if (code < 0) throw DecodeFailure(describe(code));
    codec_->flags |= AV_CODEC_FLAG_BITEXACT;
    code = avcodec_open2(codec_, decoder, nullptr);
    if (code < 0) throw DecodeFailure(describe(code));
    if (codec_->ch_layout.nb_channels < 1 || codec_->sample_rate < 1) throw DecodeFailure("声音的声道或采样率无效");

    AVChannelLayout input{};
    if (codec_->ch_layout.order == AV_CHANNEL_ORDER_UNSPEC) av_channel_layout_default(&input, codec_->ch_layout.nb_channels);
    else av_channel_layout_copy(&input, &codec_->ch_layout);
    AVChannelLayout output{};
    av_channel_layout_default(&output, 2);
    code = swr_alloc_set_opts2(&resampler_, &output, AV_SAMPLE_FMT_S16, static_cast<int>(hw::TapeSource::SAMPLE_RATE), &input, codec_->sample_fmt, codec_->sample_rate, 0, nullptr);
    av_channel_layout_uninit(&input);
    av_channel_layout_uninit(&output);
    if (code < 0) throw DecodeFailure(describe(code));
    code = swr_init(resampler_);
    if (code < 0) throw DecodeFailure(describe(code));
    packet_ = av_packet_alloc();
    frame_ = av_frame_alloc();
    if (packet_ == nullptr || frame_ == nullptr) throw DecodeFailure("内存不足");
  }

  void release() {
    av_frame_free(&frame_);
    av_packet_free(&packet_);
    swr_free(&resampler_);
    avcodec_free_context(&codec_);
    avformat_close_input(&format_);
  }

  /** 读下一个包交给解码器；文件读完时通知解码器结束。 */
  void feed() {
    for (;;) {
      const int code = av_read_frame(format_, packet_);
      if (code == AVERROR_EOF) {
        avcodec_send_packet(codec_, nullptr);
        return;
      }
      if (code < 0) throw DecodeFailure(describe(code));
      const bool mine = packet_->stream_index == streamIndex_;
      const int sent = mine ? avcodec_send_packet(codec_, packet_) : 0;
      av_packet_unref(packet_);
      if (mine && sent < 0 && sent != AVERROR(EAGAIN) && sent != AVERROR_INVALIDDATA) throw DecodeFailure(describe(sent));
      if (mine) return;
    }
  }

  /** 把一帧（为空表示取出重采样器里剩下的）重采样后追加到 samples。 */
  void convert(const AVFrame* input, std::vector<std::int16_t>& samples) {
    if (input != nullptr && startPending_) {
      startPending_ = false;
      const std::int64_t stamp = input->best_effort_timestamp;
      if (stamp != AV_NOPTS_VALUE) {
        const AVStream* stream = format_->streams[streamIndex_];
        const std::int64_t origin = stream->start_time == AV_NOPTS_VALUE ? 0 : stream->start_time;
        startSeconds_ = static_cast<double>(stamp - origin) * av_q2d(stream->time_base);
        startKnown_ = true;
      }
    }
    const int inputCount = input == nullptr ? 0 : input->nb_samples;
    const int capacity = swr_get_out_samples(resampler_, inputCount);
    if (capacity <= 0) return;
    const std::size_t before = samples.size();
    samples.resize(before + static_cast<std::size_t>(capacity) * 2);
    std::uint8_t* output[1] = {reinterpret_cast<std::uint8_t*>(samples.data() + before)};
    const int produced = swr_convert(resampler_, output, capacity, input == nullptr ? nullptr : const_cast<const std::uint8_t**>(input->extended_data), inputCount);
    if (produced < 0) {
      samples.resize(before);
      throw DecodeFailure(describe(produced));
    }
    samples.resize(before + static_cast<std::size_t>(produced) * 2);
  }

  AVFormatContext* format_{nullptr};
  AVCodecContext* codec_{nullptr};
  SwrContext* resampler_{nullptr};
  AVPacket* packet_{nullptr};
  AVFrame* frame_{nullptr};
  int streamIndex_{-1};
  bool finished_{false};
  /** 跳转后还没有解出第一帧，以及第一帧起点的换算结果。 */
  bool startPending_{false};
  bool startKnown_{false};
  double startSeconds_{0};
};

/** 探测结果。 */
struct Probe {
  double duration{0};
  bool stereo{true};
};

/** 打开一次读出时长与声道（不解码）；没有声音轨或无法解码时抛 media.tapeUnsupported。 */
Probe probe(const std::string& path) {
  try {
    const AudioInput input(path);
    return {input.duration(), input.stereo()};
  } catch (const DecodeFailure&) {
    throw shared::CoreError(api::ERROR_TAPE_UNSUPPORTED, "文件里没有声音，或格式无法解码：" + path);
  }
}

/** 解码出来的声音来源：96 kHz、16 位、立体声；第一次读取时才打开文件，倒带后重新打开，所以长磁带不占内存。 */
class LibavTapeSource final : public hw::TapeSource {
 public:
  LibavTapeSource(std::string file, Probe info, std::shared_ptr<platform::ErrorLog> errors) : file_(std::move(file)), info_(info), errors_(std::move(errors)) {}

  std::size_t read(std::int16_t* interleaved, std::size_t frames) override {
    if (failed_) return 0;
    std::size_t produced = 0;
    while (produced < frames) {
      const std::size_t available = pending_.size() / 2 - position_;
      if (available > 0) {
        const std::size_t count = std::min(available, frames - produced);
        std::memcpy(interleaved + produced * 2, pending_.data() + position_ * 2, count * 2 * sizeof(std::int16_t));
        position_ += count;
        produced += count;
        continue;
      }
      pending_.clear();
      position_ = 0;
      if (!refill()) break;
    }
    return produced;
  }

  void rewind() override {
    input_.reset();
    pending_.clear();
    position_ = 0;
    failed_ = false;
    seekPending_ = false;
    skip_ = 0;
  }

  void seek(std::uint64_t frame) override {
    if (frame == 0) {
      rewind();
      return;
    }
    input_.reset();
    pending_.clear();
    position_ = 0;
    failed_ = false;
    // 真正的跳转在下一次 read 打开文件时进行：跳到目标前的可解码位置，再丢掉多出的帧
    seekPending_ = true;
    seekTarget_ = frame;
    skip_ = 0;
  }

  [[nodiscard]] double durationSeconds() const override { return info_.duration; }
  [[nodiscard]] bool stereo() const override { return info_.stereo; }

 private:
  /** 解码出更多采样；解完或出错（当作磁带放完并写日志）时为 false。定位后把目标之前的帧丢掉。 */
  bool refill() {
    try {
      if (input_ == nullptr) {
        input_ = std::make_unique<AudioInput>(file_);
        if (seekPending_ && !input_->seekTo(static_cast<double>(seekTarget_) / hw::TapeSource::SAMPLE_RATE)) {
          // 文件不支持跳转：从头解码，把目标之前的帧都丢掉
          seekPending_ = false;
          skip_ = seekTarget_;
        }
      }
      for (;;) {
        const bool more = input_->decodeMore(pending_);
        if (seekPending_ && !pending_.empty()) {
          seekPending_ = false;
          bool known = false;
          const double start = input_->decodedStart(known);
          const auto startFrame = known ? static_cast<std::uint64_t>(std::llround(std::max(0.0, start) * hw::TapeSource::SAMPLE_RATE)) : seekTarget_;
          skip_ = seekTarget_ > startFrame ? seekTarget_ - startFrame : 0;
        }
        if (skip_ > 0 && !pending_.empty()) {
          const std::size_t drop = static_cast<std::size_t>(std::min<std::uint64_t>(skip_, pending_.size() / 2));
          pending_.erase(pending_.begin(), pending_.begin() + static_cast<std::ptrdiff_t>(drop * 2));
          skip_ -= drop;
        }
        if (!pending_.empty()) return true;
        if (!more) return false;
      }
    } catch (const DecodeFailure& error) {
      failed_ = true;
      input_.reset();
      pending_.clear();
      position_ = 0;
      if (errors_ != nullptr) errors_->write({"media", "media.tapeDecodeFailed", std::string("磁带解码失败：") + error.what(), nlohmann::json{{"file", file_}}});
      return false;
    }
  }

  std::string file_;
  Probe info_;
  std::shared_ptr<platform::ErrorLog> errors_;
  std::unique_ptr<AudioInput> input_;
  /** 已解码、还没交出去的采样（左右交错）与已交出的帧数。 */
  std::vector<std::int16_t> pending_;
  std::size_t position_{0};
  bool failed_{false};
  /** 定位待执行（下次打开文件时跳转）、目标帧，以及还要丢掉的帧数。 */
  bool seekPending_{false};
  std::uint64_t seekTarget_{0};
  std::uint64_t skip_{0};
};

/** 录音的 WAV 文件：单声道、16 位、32 kHz。 */
class WavTapeSink final : public hw::TapeSink {
 public:
  WavTapeSink(const fs::path& file, const std::string& display) : stream_(file, std::ios::binary | std::ios::trunc), display_(display) {
    if (!stream_) throw shared::CoreError(api::ERROR_SAVE_FAILED, "无法创建录音文件：" + display);
    const auto header = wavHeader(SAMPLE_RATE, 1);
    stream_.write(reinterpret_cast<const char*>(header.data()), static_cast<std::streamsize>(header.size()));
    if (!stream_) throw shared::CoreError(api::ERROR_SAVE_FAILED, "无法写入录音文件：" + display);
  }

  ~WavTapeSink() override {
    try {
      finish();
    } catch (...) {
      // 析构时无法再报告，文件头保留占位的大小。
    }
  }

  void write(const std::int16_t* samples, std::size_t count) override {
    if (finished_) return;
    writeSamples(stream_, samples, count);
    dataBytes_ += count * 2;
  }

  void finish() override {
    if (finished_) return;
    finished_ = true;
    patchWavSizes(stream_, dataBytes_);
    stream_.flush();
    const bool ok = static_cast<bool>(stream_);
    stream_.close();
    if (!ok) throw shared::CoreError(api::ERROR_SAVE_FAILED, "录音文件写入失败：" + display_);
  }

 private:
  std::ofstream stream_;
  std::string display_;
  std::uint64_t dataBytes_{0};
  bool finished_{false};
};

/**
 * 在已有的 WAV 磁带上从某个帧开始录音的去向：先写到临时文件，依次是原磁带这个位置之前的内容（构造时复制）、新录的内容（左右声道相同）、
 * 原磁带录音结束处之后的内容（finish 时复制），写完再替换原文件；中途出错或没有 finish 就销毁时原文件不动。
 */
class WavOverwriteSink final : public hw::TapeSink {
 public:
  /**
   * @param file 磁带文件。
   * @param display 显示用的路径（错误说明里用）。
   * @param original 原磁带的来源，必须是刚打开、还没有读过的。
   * @param startFrame 录音开始的帧号（原磁带的 96 kHz 帧）。
   */
  WavOverwriteSink(fs::path file, std::string display, std::unique_ptr<hw::TapeSource> original, std::uint64_t startFrame)
      : target_(std::move(file)), temp_(target_), display_(std::move(display)), original_(std::move(original)), startFrame_(startFrame) {
    temp_ += ".recording";
    stream_.open(temp_, std::ios::binary | std::ios::trunc);
    if (!stream_) throw shared::CoreError(api::ERROR_SAVE_FAILED, "无法创建录音文件：" + display_);
    try {
      const auto header = wavHeader(hw::TapeSource::SAMPLE_RATE, 2);
      stream_.write(reinterpret_cast<const char*>(header.data()), static_cast<std::streamsize>(header.size()));
      copyBefore();
      checkStream();
    } catch (...) {
      discard();
      throw;
    }
  }

  ~WavOverwriteSink() override {
    if (!finished_) discard();
  }

  void write(const std::int16_t* samples, std::size_t count) override {
    if (finished_) return;
    // 32 kHz 单声道换成 96 kHz 立体声：每个采样分成 3 帧，在上一个采样与这个采样之间线性过渡，左右声道相同
    constexpr unsigned kRatio = hw::TapeSource::SAMPLE_RATE / hw::TapeSink::SAMPLE_RATE;
    std::vector<std::int16_t> frames;
    frames.reserve(count * kRatio * 2);
    for (std::size_t index = 0; index < count; ++index) {
      const double from = havePrevious_ ? previous_ : samples[index];
      for (unsigned step = 1; step <= kRatio; ++step) {
        const auto value = static_cast<std::int16_t>(std::lround(from + (samples[index] - from) * step / kRatio));
        frames.push_back(value);
        frames.push_back(value);
      }
      previous_ = samples[index];
      havePrevious_ = true;
    }
    writeSamples(stream_, frames.data(), frames.size());
    recordedFrames_ += count * kRatio;
  }

  void finish() override {
    if (finished_) return;
    finished_ = true;
    try {
      copyAfter();
      original_.reset();
      patchWavSizes(stream_, (startFrame_ + recordedFrames_ + afterFrames_) * 4);
      stream_.flush();
      checkStream();
      stream_.close();
      std::error_code error;
      fs::rename(temp_, target_, error);
      if (error) throw shared::CoreError(api::ERROR_SAVE_FAILED, "录音文件写入失败：" + display_);
    } catch (...) {
      discard();
      throw;
    }
  }

 private:
  /** 一次复制的帧数。 */
  static constexpr std::size_t kCopyFrames = 16384;

  /** 把原磁带的前 startFrame_ 帧复制过来；原磁带比录音位置短时补静音，保证新录的内容落在录音开始的位置。 */
  void copyBefore() {
    std::vector<std::int16_t> chunk(kCopyFrames * 2);
    for (std::uint64_t remaining = startFrame_; remaining > 0;) {
      const auto want = static_cast<std::size_t>(std::min<std::uint64_t>(remaining, kCopyFrames));
      const std::size_t got = original_->read(chunk.data(), want);
      std::fill(chunk.begin() + static_cast<std::ptrdiff_t>(got * 2), chunk.begin() + static_cast<std::ptrdiff_t>(want * 2), std::int16_t{0});
      writeSamples(stream_, chunk.data(), want * 2);
      remaining -= want;
    }
  }

  /** 把原磁带录音结束处之后的内容复制过来。 */
  void copyAfter() {
    original_->seek(startFrame_ + recordedFrames_);
    std::vector<std::int16_t> chunk(kCopyFrames * 2);
    for (;;) {
      const std::size_t got = original_->read(chunk.data(), kCopyFrames);
      if (got == 0) break;
      writeSamples(stream_, chunk.data(), got * 2);
      afterFrames_ += got;
    }
  }

  void checkStream() const {
    if (!stream_) throw shared::CoreError(api::ERROR_SAVE_FAILED, "录音文件写入失败：" + display_);
  }

  /** 丢弃临时文件，原文件不动。 */
  void discard() noexcept {
    original_.reset();
    stream_.close();
    std::error_code error;
    fs::remove(temp_, error);
  }

  fs::path target_;
  fs::path temp_;
  std::string display_;
  std::unique_ptr<hw::TapeSource> original_;
  std::uint64_t startFrame_;
  std::ofstream stream_;
  std::uint64_t recordedFrames_{0};
  std::uint64_t afterFrames_{0};
  double previous_{0};
  bool havePrevious_{false};
  bool finished_{false};
};

}  // namespace

DiskTapeStore::DiskTapeStore(std::shared_ptr<platform::ErrorLog> errors) : errors_(std::move(errors)) {
  // 库默认把警告写到标准错误，核心只在解码失败时写自己的日志。
  av_log_set_level(AV_LOG_QUIET);
}

std::unique_ptr<hw::TapeSource> DiskTapeStore::open(const std::string& path) const {
  const fs::path file = toPath(path);
  std::error_code error;
  const auto status = fs::status(file, error);
  if (error || !fs::exists(status)) throw shared::CoreError(api::ERROR_FILE_NOT_FOUND, "找不到文件：" + path);
  if (!fs::is_regular_file(status)) throw shared::CoreError(api::ERROR_FILE_UNREADABLE, "不是普通文件：" + path);
  const Probe info = probe(toUtf8(file));
  return std::make_unique<LibavTapeSource>(toUtf8(file), info, errors_);
}

std::unique_ptr<hw::TapeSink> DiskTapeStore::create(const std::string& path) const { return std::make_unique<WavTapeSink>(toPath(path), path); }

std::unique_ptr<hw::TapeSink> DiskTapeStore::overwrite(const std::string& path, double startSeconds) const {
  const fs::path file = toPath(path);
  std::string extension = toUtf8(file.extension());
  std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char letter) { return static_cast<char>(std::tolower(letter)); });
  if (extension != ".wav") throw shared::CoreError(api::ERROR_TAPE_NOT_WRITABLE, "磁带文件不是 WAV，不能在它上面录音：" + path);
  auto original = open(path);
  const auto startFrame = static_cast<std::uint64_t>(std::llround(std::max(0.0, startSeconds) * hw::TapeSource::SAMPLE_RATE));
  return std::make_unique<WavOverwriteSink>(file, path, std::move(original), startFrame);
}

}  // namespace ember::infra::media
