/*
 * ------------------------------------------------------------------------
 * 名称：tape-deck.hpp
 * 说明：硬件契约：磁带机。磁带的声音来源与录音去向、磁带机的控制接口（给介质模块用）、磁带机与主机之间的信号线。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-15
 * 备注：磁带是一个声音文件：单声道时整路是数据线（Family BASIC 的 SAVE 与 LOAD），立体声时右声道是数据线（声像磁带，卡带上的电路取数据）；
 *       播放时磁带的声音（立体声取左声道，单声道取唯一的声道）经主机的声音输出播放，与主机型号和卡带有没有取数据线无关；
 *       录音记的是主机的声音输出，再加上主机经数据线送来的 1 位信号；
 *       两种磁带走的是同一个磁带机与同一组信号线；
 *       解码在契约之外：来源对磁带机只表现为 96 kHz、16 位、左右交错的采样流，由读取它的基础设施负责把任意声音文件转成这种流；
 *       信号线的时间都是主时钟换算出来的模拟时间，与真实时间无关：暂停、断电时磁带不走；
 *       磁带机是独立的外置设备，不属于主机：磁带随时可以载入与取出（没有主机、没有通电也行），换卡带、断电都不会丢；
 *       只要有主机通电并接上它（connected），不论主机型号与卡带，都能播放与录音（暂停也算通电）；主机断电或被替换时磁带机自动停止。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ember::contracts::hardware {

/** 磁带的声音来源：96 kHz、16 位、左右交错的采样流。 */
class TapeSource {
 public:
  /** 来源给出的采样率（赫兹）。 */
  static constexpr unsigned SAMPLE_RATE = 96000;

  virtual ~TapeSource() = default;

  /**
   * 取下一批采样，可能阻塞到有数据为止。
   * @param interleaved 输出缓冲，左右声道交错，至少 frames * 2 个值。
   * @param frames 请求的帧数。
   * @return 实际给出的帧数；0 表示磁带放完。
   */
  virtual std::size_t read(std::int16_t* interleaved, std::size_t frames) = 0;

  /** 倒带：下次 read 从头给出。 */
  virtual void rewind() = 0;

  /**
   * 定位：下次 read 从第 frame 帧给出；超过结尾时下次 read 返回 0。
   * 默认倒带后读掉前面的帧；能直接跳转的来源重写它。
   */
  virtual void seek(std::uint64_t frame) {
    rewind();
    std::vector<std::int16_t> scratch(SEEK_SCRATCH_FRAMES * 2);
    while (frame > 0) {
      const std::size_t count = read(scratch.data(), static_cast<std::size_t>(std::min<std::uint64_t>(frame, SEEK_SCRATCH_FRAMES)));
      if (count == 0) return;
      frame -= count;
    }
  }

  /** 总时长（秒）；不知道时为 0。 */
  [[nodiscard]] virtual double durationSeconds() const = 0;

  /** 是否是立体声磁带：是则左声道是声音、右声道是数据；否则整路是数据，两个声道相同，不出声。 */
  [[nodiscard]] virtual bool stereo() const = 0;

 private:
  /** 默认定位一次读掉的帧数。 */
  static constexpr std::size_t SEEK_SCRATCH_FRAMES = 8192;
};

/** 录音的去向：单声道、16 位的采样流。 */
class TapeSink {
 public:
  /** 录音的采样率（赫兹）。 */
  static constexpr unsigned SAMPLE_RATE = 32000;

  virtual ~TapeSink() = default;

  /** 追加一批采样。 */
  virtual void write(const std::int16_t* samples, std::size_t count) = 0;

  /** 录完：补全文件并关闭。 */
  virtual void finish() = 0;
};

/** 磁带机的工作状态。 */
enum class TapeMode : std::uint8_t {
  /** 停止。 */
  Stopped,
  /** 播放。 */
  Playing,
  /** 录音。 */
  Recording
};

/** 磁带机的控制接口：介质模块经它载入、取出、播放、录音与停止。 */
class TapeControl {
 public:
  virtual ~TapeControl() = default;

  /** 里面是否有磁带。 */
  [[nodiscard]] virtual bool loaded() const = 0;

  /** 是否接在通电的主机上：只有接上时才能播放与录音（与主机型号无关）；没接上时只能载入与取出磁带。 */
  [[nodiscard]] virtual bool connected() const = 0;

  /** 磁带的名字（文件名），没有磁带时为空。 */
  [[nodiscard]] virtual std::string name() const = 0;

  /** 工作状态。 */
  [[nodiscard]] virtual TapeMode mode() = 0;

  /** 当前位置（秒）；录音时是已录的时长。 */
  [[nodiscard]] virtual double positionSeconds() = 0;

  /** 总时长（秒）；不知道或没有磁带时为 0。 */
  [[nodiscard]] virtual double durationSeconds() const = 0;

  /** 当前的磁带是否是立体声（左声道是声音）。 */
  [[nodiscard]] virtual bool stereo() const = 0;

  /**
   * 放入一盘可以播放的磁带，位置回到开头；原来的磁带被取出。
   * @param source 声音来源。
   * @param name 显示用的名字。
   */
  virtual void load(std::unique_ptr<TapeSource> source, std::string name) = 0;

  /** 开始播放；没有磁带、没有接上主机或已在播放、录音时没有效果。 */
  virtual void play() = 0;

  /**
   * 定位到磁带的某个位置；停止与播放时都可以（播放时从新位置继续），不要求接上主机；没有磁带或在录音时没有效果。
   * @param seconds 位置（秒），小于 0 按 0，超过总时长按总时长。
   */
  virtual void seek(double seconds) = 0;

  /**
   * 放入一盘录音用的磁带并开始录音，原来的磁带被取出；没有接上主机时没有效果。
   * @param sink 录音的去向。
   * @param name 显示用的名字。
   * @param startSeconds 录音开始处在磁带上的位置（秒）：录音时的位置从它开始计；去向自己负责保留这个位置前后的原有内容。
   */
  virtual void record(std::unique_ptr<TapeSink> sink, std::string name, double startSeconds = 0.0) = 0;

  /** 停止播放或录音；录音停止时把录到的内容写完。已经停止时没有效果。 */
  virtual void stop() = 0;

  /** 取出磁带（先停止）。 */
  virtual void eject() = 0;
};

/** 数据线的比较器：把 16 位采样转成 1 位信号。采样不低于 high 时为 1，不高于 low 时为 0，之间保持原值（施密特触发）；invert 为真时输出取反。 */
struct TapeComparator {
  std::int16_t low{-3072};
  std::int16_t high{3072};
  bool invert{false};
};

/** 磁带机与主机之间的信号线。 */
class TapeLine {
 public:
  virtual ~TapeLine() = default;

  /** 磁带送给主机的 1 位信号（没有播放时保持最后的值）。 */
  [[nodiscard]] virtual bool level() = 0;

  /** 主机送给磁带机的 1 位信号；录音时记录下来。 */
  virtual void drive(bool high) = 0;

  /** 设置比较器；默认是 ±3072 的施密特触发、不取反。 */
  virtual void setComparator(const TapeComparator& comparator) = 0;

  /**
   * 设置边沿通知：播放时数据线每次翻转，在翻转的时刻回调；参数是翻转后的电平与距上一次翻转的采样数（96 kHz）。
   * 传空函数取消。
   */
  virtual void setEdgeListener(std::function<void(bool level, std::uint32_t samplesSincePrevious)> listener) = 0;
};

}  // namespace ember::contracts::hardware
