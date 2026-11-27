/*
 * ------------------------------------------------------------------------
 * 名称：media-manager.hpp
 * 说明：介质服务的实现：软驱盘片的插入、取出、保存、状态与故障注入，磁带机的载入、取出、播放、录音与状态。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-16
 * 备注：镜像文件的读取在请求线程完成，不碰硬件状态；软驱与磁带机都是独立于主机的设备，不经主机找到，直接用共享的那一台；
 *       它们的状态只在模拟线程上读写，所以都经 runOnSimulation（不要求有主机、不要求通电）；
 *       盘片镜像由驱动器持有，是唯一的数据；学习机写盘只改内存里的镜像并记下已修改的字节区间，本服务订阅驱动器的修改通知（区间从空变为非空时来一次），
 *       后台线程收到通知就立即在模拟线程上把这个区间直接写进镜像文件，写成功才清除“已修改”（没有定时检查）；
 *       反过来，盘片放入后监视镜像文件（系统的文件变化通知，不轮询），文件被别的程序改动时，在模拟线程上读出文件、和内存里的镜像逐字节比较，
 *       把不同的部分同步进镜像（已修改区间里尚未写回的字节保留，之后写回文件）；自己写文件引起的通知比较后没有差别，不会循环；
 *       文件大小与盘片不同或读不出来（别的程序正在保存的中间状态）时忽略，等下一次变化；
 *       所有读写镜像文件的动作都在模拟线程上进行，彼此按顺序执行，不会用旧内容覆盖新内容；
 *       写回失败时区间保留，发 media.floppySaveFailed（成功前只发一次）并写错误日志，后台线程之后每隔重试间隔重试一次；
 *       取出与换盘前、核心停止（shutdown）时也先把未写回的修改写回；
 *       磁带载入后（录音结束后录好的文件也算）用一个后台线程读一遍声音得出时间轴（读的是独立打开的另一份来源，不碰磁带机），结果只存在内存里；
 *       在已有的磁带上录音时，复制录音位置之前内容的去向由存取服务在请求线程里创建（不占模拟线程），录音结束时去向把后面的内容接上并替换磁带文件；
 *       每盘磁带有自己的序号，取出或换磁带时先取消并等待上一份分析结束，分析结果只在序号仍是当前的磁带时才采用。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "contracts/hardware/floppy-activity.hpp"
#include "contracts/hardware/floppy-drive.hpp"
#include "contracts/hardware/tape-deck.hpp"
#include "contracts/media/floppy-image-store.hpp"
#include "contracts/media/media-service.hpp"
#include "contracts/media/printer-record.hpp"
#include "contracts/media/tape-store.hpp"
#include "contracts/runtime/runtime-service.hpp"
#include "platform/events/event-sink.hpp"
#include "platform/files/file-watcher.hpp"
#include "platform/logging/error-log.hpp"

namespace ember::logic::media {

/** 介质服务；同时接收软驱马达的开关通知（变化时发 media.changed，软驱状态里的马达字段随之变化）。 */
class MediaManager final : public contracts::media::MediaService, public contracts::hardware::FloppyActivity {
 public:
  /**
   * @param deck 磁带机：独立于主机的设备，与主机型号目录共用同一台。
   * @param floppyDrives 软驱（按编号排列）：同样是独立于主机的设备，与主机型号目录共用。
   * @param errors 写回失败时写错误日志；为空时不写。
   * @param watchFactory 监视镜像文件的工厂；为空时不监视（文件被外部改动不会同步）。
   * @param autoSave 是否订阅驱动器的修改通知并自动写回、监视文件并同步外部改动；为 false 时不启动后台线程（取出、换盘与停止时的写回仍然有效）。
   * @param retryInterval 自动写回失败后重试的间隔。
   */
  MediaManager(std::shared_ptr<contracts::media::FloppyImageStore> store, std::shared_ptr<contracts::media::TapeStore> tapeStore, std::shared_ptr<contracts::runtime::RuntimeService> runtime,
               std::shared_ptr<platform::EventSink> events, std::shared_ptr<contracts::media::PrinterRecord> printerRecord, std::shared_ptr<contracts::hardware::TapeControl> deck,
               std::vector<std::shared_ptr<contracts::hardware::FloppyDrive>> floppyDrives, std::shared_ptr<platform::ErrorLog> errors = nullptr,
               std::shared_ptr<platform::FileWatchFactory> watchFactory = nullptr, bool autoSave = true, std::chrono::milliseconds retryInterval = std::chrono::seconds(5));
  ~MediaManager() override;

  MediaManager(const MediaManager&) = delete;
  MediaManager& operator=(const MediaManager&) = delete;

  /** 停止后台线程并把所有未写回的修改写回；在运行服务停止之前调用（核心停止时），可重复调用。 */
  void shutdown();

  contracts::media::MediaFloppyInsertResult floppyInsert(const contracts::media::MediaFloppyInsertParams& params) override;
  contracts::media::MediaFloppyEjectResult floppyEject(const contracts::media::MediaFloppyEjectParams& params) override;
  contracts::media::MediaFloppySaveResult floppySave(const contracts::media::MediaFloppySaveParams& params) override;
  contracts::media::MediaFloppyWriteProtectResult floppyWriteProtect(const contracts::media::MediaFloppyWriteProtectParams& params) override;
  [[nodiscard]] contracts::media::MediaFloppyStatusResult floppyStatus() override;
  contracts::media::MediaFloppyFaultResult floppyFault(const contracts::media::MediaFloppyFaultParams& params) override;
  [[nodiscard]] contracts::media::MediaPrinterStatusResult printerStatus() override;
  contracts::media::MediaPrinterFlushResult printerFlush() override;
  contracts::media::MediaTapeLoadResult tapeLoad(const contracts::media::MediaTapeLoadParams& params) override;
  contracts::media::MediaTapeEjectResult tapeEject() override;
  contracts::media::MediaTapePlayResult tapePlay() override;
  contracts::media::MediaTapeRecordResult tapeRecord(const contracts::media::MediaTapeRecordParams& params) override;
  contracts::media::MediaTapeStopResult tapeStop() override;
  [[nodiscard]] contracts::media::MediaTapeStatusResult tapeStatus() override;
  contracts::media::MediaTapeSeekResult tapeSeek(const contracts::media::MediaTapeSeekParams& params) override;
  [[nodiscard]] contracts::media::MediaTapeTimelineResult tapeTimeline() override;
  void motorChanged(unsigned drive, bool on) override;

 private:
  /**
   * 在模拟线程上取驱动器并执行动作，返回动作做完后的驱动器状态；不需要主机。
   * @throws shared::CoreError media.noDrive，以及动作里抛出的错误。
   */
  template <typename Action>
  contracts::media::FloppyStatus withDrive(std::int64_t drive, Action action);

  /** 发 media.changed。 */
  void publishChanged(std::int64_t drive);

  /**
   * 把驱动器里已修改的区间写进镜像文件并清除；没有修改时什么都不做。在模拟线程上调用。
   * @return 是否写了文件。
   * @throws shared::CoreError media.saveFailed（区间保留）。
   */
  bool writeBack(std::size_t index);

  /**
   * 把镜像文件被外部改动的部分同步进驱动器里的镜像（已修改区间里的字节保留）。在模拟线程上调用。
   * @return 是否改动了镜像。
   */
  bool pullFromFile(std::size_t index);

  /** 重新监视一个驱动器的镜像文件；path 为空表示只停止监视。 */
  void rewatch(std::size_t index, const std::string& path);

  /** 订阅者与后台线程共用的信号；驱动器的监听者与文件监视各持有它的一份，所以服务销毁后再通知也不会访问已释放的对象。 */
  struct ChangeSignal {
    std::mutex mutex;
    std::condition_variable wake;
    /** 各驱动器的内存镜像有了新的修改。 */
    std::vector<char> memoryChanged;
    /** 各驱动器的镜像文件被改动。 */
    std::vector<char> fileChanged;
    bool stopping{false};

    /** 标记一个驱动器的变化并唤醒后台线程；可在任意线程调用。 */
    void raise(std::vector<char> ChangeSignal::* which, std::size_t index) {
      {
        std::lock_guard lock(mutex);
        (this->*which)[index] = 1;
      }
      wake.notify_one();
    }
  };

  /** 停止后台线程并等它结束。 */
  void stopThread();

  /** 后台线程的主循环：等通知（内存镜像有修改、镜像文件被改动），收到就处理对应的驱动器；有写回失败时每隔重试间隔再试。 */
  void autoSaveLoop();

  /** 后台线程写回一个驱动器；failed 记录它上一次写回是否失败，完成后更新。 */
  void autoSaveDrive(std::size_t index, bool& failed);

  /** 后台线程把一个驱动器的镜像文件的外部改动同步进镜像。 */
  void autoPullDrive(std::size_t index);

  /**
   * 在模拟线程上用磁带机执行动作，返回动作做完后的磁带机状态；不需要主机。
   * @throws 动作里抛出的错误。
   */
  template <typename Action>
  contracts::media::TapeStatus withDeck(Action action);

  /** 发 media.changed（磁带机）。 */
  void publishTapeChanged();

  /** 在磁带机里已有的磁带上从当前位置开始录音（tapeRecord 没有给文件时）。 */
  contracts::media::MediaTapeRecordResult recordOverLoadedTape();

  /** 磁带机当前的状态：磁带机自己的状态加上磁带的序号。在模拟线程上调用。 */
  contracts::media::TapeStatus readTape();

  /** 停止上一盘磁带的分析（等后台线程结束），把时间轴换成这盘磁带的初始状态；path 为空表示没有可分析的磁带（state 为 none），否则启动后台分析。 */
  void restartAnalysis(const std::string& path, std::int64_t serial);

  /** 后台分析的线程体：读一遍磁带算出时间轴，结束后发 media.tapeAnalyzed。 */
  void analyzeTapeFile(const std::string& path, std::int64_t serial, const std::shared_ptr<std::atomic<bool>>& cancel);

  /** 只在这盘磁带还是当前的磁带时，用 change 修改时间轴。 */
  template <typename Change>
  void updateTimeline(std::int64_t serial, Change change);

  std::shared_ptr<contracts::media::FloppyImageStore> store_;
  std::shared_ptr<contracts::media::TapeStore> tapeStore_;
  std::shared_ptr<contracts::runtime::RuntimeService> runtime_;
  std::shared_ptr<platform::EventSink> events_;
  std::shared_ptr<contracts::media::PrinterRecord> printerRecord_;
  std::shared_ptr<contracts::hardware::TapeControl> deck_;
  std::vector<std::shared_ptr<contracts::hardware::FloppyDrive>> drives_;
  std::shared_ptr<platform::ErrorLog> errors_;
  std::shared_ptr<platform::FileWatchFactory> watchFactory_;
  bool autoSave_;
  std::chrono::milliseconds retryInterval_;
  std::shared_ptr<ChangeSignal> signal_ = std::make_shared<ChangeSignal>();
  /** 各驱动器镜像文件的监视（由 watchMutex_ 保护）。 */
  std::mutex watchMutex_;
  std::vector<std::unique_ptr<platform::FileWatcher>> watchers_;
  std::thread autoSaveThread_;
  /** 磁带的序号：每放入、取出或换掉一盘磁带加 1。 */
  std::atomic<std::int64_t> tapeSerial_{0};
  /** 时间轴的最新结果（由 timelineMutex_ 保护）：只存在内存里。 */
  std::mutex timelineMutex_;
  contracts::media::TapeTimeline timeline_;
  /** 分析线程与它的取消标志（由 analysisMutex_ 保护，启动与等待结束都在锁里）。 */
  std::mutex analysisMutex_;
  std::thread analysisThread_;
  std::shared_ptr<std::atomic<bool>> analysisCancel_;
};

}  // namespace ember::logic::media
