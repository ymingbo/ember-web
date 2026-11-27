/*
 * ------------------------------------------------------------------------
 * 名称：media-manager.cpp
 * 说明：介质服务的实现。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-16
 * ------------------------------------------------------------------------
 */

#include "logic/media/media-manager.hpp"

#include <algorithm>
#include <optional>
#include <utility>

#include "contracts/media/media-error.hpp"
#include "logic/media/tape-analysis.hpp"
#include "shared/errors/core-error.hpp"

namespace ember::logic::media {

namespace api = contracts::media;
namespace hw = contracts::hardware;

namespace {

/** 路径里最后一段（文件名）；两种分隔符都认。 */
std::string baseName(const std::string& path) {
  const auto position = path.find_last_of("/\\");
  return position == std::string::npos ? path : path.substr(position + 1);
}

/** 驱动器当前的状态。 */
api::FloppyStatus statusOf(std::int64_t index, const hw::FloppyDrive& drive) {
  api::FloppyStatus status;
  status.drive = index;
  status.inserted = drive.inserted();
  if (drive.inserted() || drive.mediaSuspended()) status.fileName = baseName(drive.source());
  status.writeProtected = drive.writeProtected();
  status.modified = drive.modified();
  status.size = static_cast<std::int64_t>(drive.image().size());
  status.cylinder = drive.cylinder();
  status.motor = drive.motorRunning();
  status.faults.ejected = drive.mediaSuspended();
  status.faults.noIndex = drive.faults().noIndex;
  status.faults.seekFail = drive.faults().seekFail;
  status.faults.unresponsive = drive.faults().unresponsive;
  for (const int sector : drive.faults().badSectors) status.faults.badSectors.push_back(sector);
  return status;
}

/** 磁带机当前的状态。 */
api::TapeStatus tapeStatusOf(hw::TapeControl& deck) {
  api::TapeStatus status;
  status.connected = deck.connected();
  status.loaded = deck.loaded();
  if (status.loaded) status.fileName = baseName(deck.name());
  switch (deck.mode()) {
    case hw::TapeMode::Playing:
      status.mode = "playing";
      break;
    case hw::TapeMode::Recording:
      status.mode = "recording";
      break;
    case hw::TapeMode::Stopped:
      status.mode = "stopped";
      break;
  }
  status.position = deck.positionSeconds();
  status.duration = deck.durationSeconds();
  status.stereo = deck.stereo();
  return status;
}

}  // namespace

MediaManager::MediaManager(std::shared_ptr<api::FloppyImageStore> store, std::shared_ptr<api::TapeStore> tapeStore, std::shared_ptr<contracts::runtime::RuntimeService> runtime,
                           std::shared_ptr<platform::EventSink> events, std::shared_ptr<api::PrinterRecord> printerRecord, std::shared_ptr<hw::TapeControl> deck,
                           std::vector<std::shared_ptr<hw::FloppyDrive>> floppyDrives, std::shared_ptr<platform::ErrorLog> errors, std::shared_ptr<platform::FileWatchFactory> watchFactory, bool autoSave,
                           std::chrono::milliseconds retryInterval)
    : store_(std::move(store)),
      tapeStore_(std::move(tapeStore)),
      runtime_(std::move(runtime)),
      events_(std::move(events)),
      printerRecord_(std::move(printerRecord)),
      deck_(std::move(deck)),
      drives_(std::move(floppyDrives)),
      errors_(std::move(errors)),
      watchFactory_(std::move(watchFactory)),
      autoSave_(autoSave),
      retryInterval_(retryInterval) {
  timeline_.state = "none";
  watchers_.resize(drives_.size());
  if (!autoSave_) return;
  signal_->memoryChanged.assign(drives_.size(), 0);
  signal_->fileChanged.assign(drives_.size(), 0);
  for (std::size_t index = 0; index < drives_.size(); ++index) {
    // 监听者只持有信号，不持有本服务。
    drives_[index]->setModifiedListener([signal = signal_, index] { signal->raise(&ChangeSignal::memoryChanged, index); });
  }
  autoSaveThread_ = std::thread([this] { autoSaveLoop(); });
}

MediaManager::~MediaManager() { stopThread(); }

void MediaManager::stopThread() {
  restartAnalysis({}, tapeSerial_.load());
  {
    std::lock_guard lock(watchMutex_);
    for (auto& watcher : watchers_) watcher.reset();
  }
  {
    std::lock_guard lock(signal_->mutex);
    signal_->stopping = true;
  }
  signal_->wake.notify_all();
  if (autoSaveThread_.joinable()) autoSaveThread_.join();
}

void MediaManager::shutdown() {
  stopThread();
  for (std::size_t index = 0; index < drives_.size(); ++index) {
    try {
      runtime_->runOnSimulation([&] { writeBack(index); });
    } catch (const shared::CoreError& error) {
      // 运行服务已经停止时没有可写回的内存镜像；其他错误写进日志。
      if (error.code() != shared::ERROR_UNAVAILABLE && errors_ != nullptr) errors_->write({"media", error.code(), error.what(), nlohmann::json{{"drive", index}}});
    }
  }
}

template <typename Action>
api::FloppyStatus MediaManager::withDrive(std::int64_t drive, Action action) {
  if (drive < 0 || drive >= static_cast<std::int64_t>(drives_.size())) throw shared::CoreError(api::ERROR_NO_DRIVE, "没有软驱 " + std::to_string(drive));
  std::optional<api::FloppyStatus> status;
  runtime_->runOnSimulation([&] {
    hw::FloppyDrive& unit = *drives_[static_cast<std::size_t>(drive)];
    action(unit);
    status = statusOf(drive, unit);
  });
  return *status;
}

void MediaManager::publishChanged(std::int64_t drive) { events_->publish(api::MEDIA_CHANGED_EVENT, nlohmann::json(api::MediaChangedData{"floppy", drive})); }

void MediaManager::publishTapeChanged() { events_->publish(api::MEDIA_CHANGED_EVENT, nlohmann::json(api::MediaChangedData{"tape", 0})); }

template <typename Action>
api::TapeStatus MediaManager::withDeck(Action action) {
  std::optional<api::TapeStatus> status;
  runtime_->runOnSimulation([&] {
    action(*deck_);
    status = readTape();
  });
  return *status;
}

api::TapeStatus MediaManager::readTape() {
  api::TapeStatus status = tapeStatusOf(*deck_);
  status.serial = tapeSerial_.load();
  return status;
}

template <typename Change>
void MediaManager::updateTimeline(std::int64_t serial, Change change) {
  std::lock_guard lock(timelineMutex_);
  if (timeline_.serial == serial) change(timeline_);
}

void MediaManager::restartAnalysis(const std::string& path, std::int64_t serial) {
  std::lock_guard control(analysisMutex_);
  if (analysisCancel_ != nullptr) analysisCancel_->store(true);
  if (analysisThread_.joinable()) analysisThread_.join();
  {
    std::lock_guard lock(timelineMutex_);
    timeline_ = api::TapeTimeline{};
    timeline_.serial = serial;
    timeline_.state = path.empty() ? "none" : "analyzing";
  }
  analysisCancel_.reset();
  if (path.empty()) return;
  analysisCancel_ = std::make_shared<std::atomic<bool>>(false);
  analysisThread_ = std::thread([this, path, serial, cancel = analysisCancel_] { analyzeTapeFile(path, serial, cancel); });
}

void MediaManager::analyzeTapeFile(const std::string& path, std::int64_t serial, const std::shared_ptr<std::atomic<bool>>& cancel) {
  std::optional<TapeAnalysis> analysis;
  try {
    // 独立打开另一份来源来读，不影响磁带机里正在播放的那一份
    const auto source = tapeStore_->open(path);
    analysis = analyzeTape(*source, [&](double progress) { updateTimeline(serial, [progress](api::TapeTimeline& timeline) { timeline.progress = progress; }); }, *cancel);
  } catch (const shared::CoreError& error) {
    if (errors_ != nullptr) errors_->write({"media", error.code(), error.what(), nlohmann::json{{"file", path}}});
  }
  // 被取消：取出或换磁带的请求已经把时间轴换成新的状态，不再采用结果
  if (cancel->load()) return;
  updateTimeline(serial, [&analysis](api::TapeTimeline& timeline) {
    if (!analysis) {
      timeline.state = "failed";
      return;
    }
    timeline.state = "ready";
    timeline.progress = 1;
    timeline.duration = analysis->duration;
    timeline.step = analysis->step;
    timeline.peaks.assign(analysis->peaks.begin(), analysis->peaks.end());
    for (const auto& [start, end] : analysis->blanks) timeline.blanks.push_back(api::TapeBlank{start, end});
  });
  events_->publish(api::MEDIA_TAPE_ANALYZED_EVENT, nlohmann::json(api::MediaTapeAnalyzedData{serial}));
}

api::MediaFloppyInsertResult MediaManager::floppyInsert(const api::MediaFloppyInsertParams& params) {
  std::vector<std::uint8_t> image = store_->read(params.path);
  const bool writeProtected = params.writeProtected.value_or(false);
  // 替换盘片之前先写回旧盘片的修改；写回失败时盘片不换。
  auto status = withDrive(params.drive, [&](hw::FloppyDrive& drive) {
    writeBack(static_cast<std::size_t>(params.drive));
    drive.insertImage(std::move(image), writeProtected, params.path);
  });
  rewatch(static_cast<std::size_t>(params.drive), params.path);
  publishChanged(params.drive);
  return api::MediaFloppyInsertResult{std::move(status)};
}

api::MediaFloppyEjectResult MediaManager::floppyEject(const api::MediaFloppyEjectParams& params) {
  auto status = withDrive(params.drive, [&](hw::FloppyDrive& drive) {
    writeBack(static_cast<std::size_t>(params.drive));
    drive.ejectImage();
  });
  rewatch(static_cast<std::size_t>(params.drive), {});
  publishChanged(params.drive);
  return api::MediaFloppyEjectResult{std::move(status)};
}

api::MediaFloppySaveResult MediaManager::floppySave(const api::MediaFloppySaveParams& params) {
  auto status = withDrive(params.drive, [&](hw::FloppyDrive& drive) {
    if (!drive.inserted() && !drive.mediaSuspended()) throw shared::CoreError(api::ERROR_NO_DISK, "驱动器里没有盘片");
    store_->write(drive.source(), drive.image());
    drive.markSaved();
  });
  publishChanged(params.drive);
  return api::MediaFloppySaveResult{std::move(status)};
}

bool MediaManager::writeBack(std::size_t index) {
  hw::FloppyDrive& drive = *drives_[index];
  const hw::FloppyDirtyRange range = drive.dirtyRange();
  if (range.empty()) return false;
  store_->writeAt(drive.source(), range.begin, std::span<const std::uint8_t>(drive.image()).subspan(range.begin, range.end - range.begin));
  drive.markSaved();
  return true;
}

void MediaManager::autoSaveLoop() {
  std::vector<bool> failed(drives_.size(), false);
  std::unique_lock lock(signal_->mutex);
  while (true) {
    const auto ready = [this] {
      return signal_->stopping || std::find(signal_->memoryChanged.begin(), signal_->memoryChanged.end(), 1) != signal_->memoryChanged.end() ||
             std::find(signal_->fileChanged.begin(), signal_->fileChanged.end(), 1) != signal_->fileChanged.end();
    };
    // 没有写回失败时只等通知；有失败时最多等一个重试间隔就再试。
    if (std::find(failed.begin(), failed.end(), true) == failed.end()) {
      signal_->wake.wait(lock, ready);
    } else {
      signal_->wake.wait_for(lock, retryInterval_, ready);
    }
    if (signal_->stopping) break;
    const std::vector<char> filesChanged = std::exchange(signal_->fileChanged, std::vector<char>(drives_.size(), 0));
    const std::vector<char> memoryChanged = std::exchange(signal_->memoryChanged, std::vector<char>(drives_.size(), 0));
    lock.unlock();
    for (std::size_t index = 0; index < drives_.size(); ++index) {
      // 先同步外部改动（已修改区间里的字节保留），再写回学习机的修改。
      if (filesChanged[index] != 0) autoPullDrive(index);
      if (memoryChanged[index] == 0 && !failed[index]) continue;
      bool driveFailed = failed[index];
      autoSaveDrive(index, driveFailed);
      failed[index] = driveFailed;
    }
    lock.lock();
  }
}

void MediaManager::autoPullDrive(std::size_t index) {
  try {
    bool changed = false;
    runtime_->runOnSimulation([&] { changed = pullFromFile(index); });
    if (changed) publishChanged(static_cast<std::int64_t>(index));
  } catch (const shared::CoreError&) {
    // 运行服务已经停止。
  }
}

bool MediaManager::pullFromFile(std::size_t index) {
  hw::FloppyDrive& drive = *drives_[index];
  if ((!drive.inserted() && !drive.mediaSuspended()) || drive.source().empty()) return false;
  std::vector<std::uint8_t> file;
  try {
    file = store_->read(drive.source());
  } catch (const shared::CoreError&) {
    // 别的程序正在保存、文件被删除等中间状态；等下一次变化。
    return false;
  }
  const std::vector<std::uint8_t>& image = drive.image();
  if (file.size() != image.size()) return false;
  const hw::FloppyDirtyRange dirty = drive.dirtyRange();
  // 已修改区间里的字节是学习机刚写、还没写进文件的，不能被文件里的旧内容盖掉。
  const auto differs = [&](std::size_t position) { return file[position] != image[position] && !(position >= dirty.begin && position < dirty.end); };
  bool changed = false;
  std::size_t position = 0;
  while (position < file.size()) {
    if (!differs(position)) {
      ++position;
      continue;
    }
    std::size_t end = position;
    while (end < file.size() && differs(end)) ++end;
    drive.applyExternalChange(position, std::span<const std::uint8_t>(file).subspan(position, end - position));
    changed = true;
    position = end;
  }
  return changed;
}

void MediaManager::rewatch(std::size_t index, const std::string& path) {
  if (!autoSave_ || watchFactory_ == nullptr) return;
  std::lock_guard lock(watchMutex_);
  watchers_[index].reset();
  if (path.empty()) return;
  watchers_[index] = watchFactory_->watch(path, [signal = signal_, index] { signal->raise(&ChangeSignal::fileChanged, index); });
  if (watchers_[index] == nullptr) {
    if (errors_ != nullptr) errors_->write({"media", std::string(api::ERROR_FILE_UNREADABLE), "无法监视镜像文件，它被外部修改时不会自动同步", nlohmann::json{{"drive", index}, {"path", path}}});
    return;
  }
  // 读入到开始监视之间文件可能已经变了，先同步一次。
  signal_->raise(&ChangeSignal::fileChanged, index);
}

void MediaManager::autoSaveDrive(std::size_t index, bool& failed) {
  try {
    bool wrote = false;
    runtime_->runOnSimulation([&] { wrote = writeBack(index); });
    failed = false;
    if (wrote) publishChanged(static_cast<std::int64_t>(index));
  } catch (const shared::CoreError& error) {
    // 运行服务已经停止；不算写回失败。
    if (error.code() == shared::ERROR_UNAVAILABLE) return;
    if (failed) return;
    failed = true;
    if (errors_ != nullptr) errors_->write({"media", error.code(), error.what(), nlohmann::json{{"drive", index}}});
    events_->publish(api::MEDIA_FLOPPY_SAVE_FAILED_EVENT, nlohmann::json(api::MediaFloppySaveFailedData{static_cast<std::int64_t>(index), error.what()}));
  }
}

api::MediaFloppyWriteProtectResult MediaManager::floppyWriteProtect(const api::MediaFloppyWriteProtectParams& params) {
  auto status = withDrive(params.drive, [&](hw::FloppyDrive& drive) { drive.setWriteProtected(params.writeProtected); });
  publishChanged(params.drive);
  return api::MediaFloppyWriteProtectResult{std::move(status)};
}

api::MediaFloppyStatusResult MediaManager::floppyStatus() {
  api::MediaFloppyStatusResult result;
  runtime_->runOnSimulation([&] {
    for (std::size_t index = 0; index < drives_.size(); ++index) result.drives.push_back(statusOf(static_cast<std::int64_t>(index), *drives_[index]));
  });
  return result;
}

api::MediaFloppyFaultResult MediaManager::floppyFault(const api::MediaFloppyFaultParams& params) {
  const bool enabled = params.enabled.value_or(true);
  if (params.fault == "badSector" && !params.sector) throw shared::CoreError(api::ERROR_SECTOR_REQUIRED, "指定扇区校验错必须给出扇区号");
  auto status = withDrive(params.drive, [&](hw::FloppyDrive& drive) {
    hw::FloppyFaults& faults = drive.mutableFaults();
    if (params.fault == "ejected") {
      if (enabled) {
        drive.suspendMedia();
      } else {
        drive.restoreMedia();
      }
    } else if (params.fault == "writeProtected") {
      drive.setWriteProtected(enabled);
    } else if (params.fault == "noIndex") {
      faults.noIndex = enabled;
    } else if (params.fault == "seekFail") {
      faults.seekFail = enabled;
    } else if (params.fault == "unresponsive") {
      faults.unresponsive = enabled;
    } else if (params.fault == "badSector") {
      const int sector = static_cast<int>(*params.sector);
      auto& bad = faults.badSectors;
      const auto found = std::find(bad.begin(), bad.end(), sector);
      if (enabled && found == bad.end()) bad.push_back(sector);
      if (!enabled && found != bad.end()) bad.erase(found);
    } else {
      // clear：关闭全部故障，放回盘片，取消写保护。
      faults = hw::FloppyFaults{};
      drive.restoreMedia();
      drive.setWriteProtected(false);
    }
  });
  publishChanged(params.drive);
  return api::MediaFloppyFaultResult{std::move(status)};
}

api::MediaPrinterStatusResult MediaManager::printerStatus() {
  const api::PrinterRecordSnapshot record = printerRecord_->snapshot();
  api::PrinterStatus status;
  status.state = "idle";
  status.pages = record.pages;
  status.directory = record.directory;
  if (!record.lastFile.empty()) status.lastFile = record.lastFile;
  runtime_->useMachine([&](hw::Machine& machine) {
    const hw::PrinterControl* printer = machine.printer();
    if (printer == nullptr || !machine.isPoweredOn()) return;
    status.hasContent = printer->pageHasContent();
    if (status.hasContent) {
      status.state = "printing";
    } else if (printer->pagesOutput() > 0) {
      status.state = "done";
    }
  });
  return api::MediaPrinterStatusResult{std::move(status)};
}

api::MediaPrinterFlushResult MediaManager::printerFlush() {
  bool missingPrinter = false;
  bool empty = false;
  const bool hasMachine = runtime_->useMachine([&](hw::Machine& machine) {
    hw::PrinterControl* printer = machine.printer();
    if (printer == nullptr) {
      missingPrinter = true;
    } else if (!printer->pageHasContent()) {
      empty = true;
    } else {
      printer->flushPage();
    }
  });
  if (!hasMachine) throw shared::CoreError(api::ERROR_NO_MACHINE, "还没有主机，请先打开卡带或用整机机型开机");
  if (missingPrinter) throw shared::CoreError(api::ERROR_NO_PRINTER, "这台主机没有打印机");
  if (empty) throw shared::CoreError(api::ERROR_PRINTER_EMPTY, "打印机当前页上没有内容");
  return api::MediaPrinterFlushResult{printerStatus().status};
}

void MediaManager::motorChanged(unsigned drive, bool /*on*/) { publishChanged(static_cast<std::int64_t>(drive)); }

api::MediaTapeLoadResult MediaManager::tapeLoad(const api::MediaTapeLoadParams& params) {
  auto source = tapeStore_->open(params.path);
  auto status = withDeck([&](hw::TapeControl& deck) {
    if (deck.mode() != hw::TapeMode::Stopped) throw shared::CoreError(api::ERROR_TAPE_BUSY, "磁带机正在播放或录音，请先停止");
    deck.load(std::move(source), params.path);
    ++tapeSerial_;
  });
  restartAnalysis(params.path, status.serial);
  publishTapeChanged();
  return api::MediaTapeLoadResult{std::move(status)};
}

api::MediaTapeEjectResult MediaManager::tapeEject() {
  bool hadTape = false;
  auto status = withDeck([&](hw::TapeControl& deck) {
    hadTape = deck.loaded();
    deck.eject();
    if (hadTape) ++tapeSerial_;
  });
  if (hadTape) restartAnalysis({}, status.serial);
  publishTapeChanged();
  return api::MediaTapeEjectResult{std::move(status)};
}

api::MediaTapePlayResult MediaManager::tapePlay() {
  auto status = withDeck([](hw::TapeControl& deck) {
    if (!deck.loaded()) throw shared::CoreError(api::ERROR_NO_TAPE, "磁带机里没有磁带");
    if (!deck.connected()) throw shared::CoreError(api::ERROR_TAPE_NOT_CONNECTED, "主机没有通电，磁带机不能播放");
    deck.play();
  });
  publishTapeChanged();
  return api::MediaTapePlayResult{std::move(status)};
}

api::MediaTapeRecordResult MediaManager::tapeRecord(const api::MediaTapeRecordParams& params) {
  if (!params.path) return recordOverLoadedTape();
  const std::string& path = *params.path;
  auto status = withDeck([&](hw::TapeControl& deck) {
    if (deck.mode() != hw::TapeMode::Stopped) throw shared::CoreError(api::ERROR_TAPE_BUSY, "磁带机正在播放或录音，请先停止");
    if (!deck.connected()) throw shared::CoreError(api::ERROR_TAPE_NOT_CONNECTED, "主机没有通电，磁带机不能录音");
    deck.record(tapeStore_->create(path), path);
    ++tapeSerial_;
  });
  restartAnalysis({}, status.serial);
  publishTapeChanged();
  return api::MediaTapeRecordResult{std::move(status)};
}

api::MediaTapeRecordResult MediaManager::recordOverLoadedTape() {
  std::string path;
  double start = 0;
  std::int64_t serial = 0;
  withDeck([&](hw::TapeControl& deck) {
    if (!deck.loaded()) throw shared::CoreError(api::ERROR_NO_TAPE, "磁带机里没有磁带，请先选择录音文件");
    if (deck.mode() != hw::TapeMode::Stopped) throw shared::CoreError(api::ERROR_TAPE_BUSY, "磁带机正在播放或录音，请先停止");
    if (!deck.connected()) throw shared::CoreError(api::ERROR_TAPE_NOT_CONNECTED, "主机没有通电，磁带机不能录音");
    path = deck.name();
    start = deck.positionSeconds();
    serial = tapeSerial_.load();
  });
  // 复制录音位置之前的内容要一段时间，不占用模拟线程；开始录音时磁带必须还是刚才那盘
  auto sink = tapeStore_->overwrite(path, start);
  auto status = withDeck([&](hw::TapeControl& deck) {
    if (!deck.loaded() || deck.mode() != hw::TapeMode::Stopped || tapeSerial_.load() != serial) throw shared::CoreError(api::ERROR_TAPE_BUSY, "磁带已经变了，请重新开始录音");
    deck.record(std::move(sink), path, start);
    if (deck.mode() != hw::TapeMode::Recording) throw shared::CoreError(api::ERROR_TAPE_NOT_CONNECTED, "主机没有通电，磁带机不能录音");
    ++tapeSerial_;
  });
  restartAnalysis({}, status.serial);
  publishTapeChanged();
  return api::MediaTapeRecordResult{std::move(status)};
}

api::MediaTapeStopResult MediaManager::tapeStop() {
  std::string recordedPath;
  double stopPosition = 0;
  auto status = withDeck([&](hw::TapeControl& deck) {
    if (deck.mode() == hw::TapeMode::Recording) {
      recordedPath = deck.name();
      stopPosition = deck.positionSeconds();
    }
    deck.stop();
  });
  if (!recordedPath.empty()) {
    // 录好的文件自动成为磁带机里的磁带，位置停在录音停止处，可以直接播放。
    try {
      auto source = tapeStore_->open(recordedPath);
      status = withDeck([&](hw::TapeControl& deck) {
        deck.load(std::move(source), recordedPath);
        deck.seek(stopPosition);
        ++tapeSerial_;
      });
      restartAnalysis(recordedPath, status.serial);
    } catch (...) {
      publishTapeChanged();
      throw;
    }
  }
  publishTapeChanged();
  return api::MediaTapeStopResult{std::move(status)};
}

api::MediaTapeStatusResult MediaManager::tapeStatus() {
  api::TapeStatus status;
  runtime_->runOnSimulation([&] { status = readTape(); });
  return api::MediaTapeStatusResult{std::move(status)};
}

api::MediaTapeSeekResult MediaManager::tapeSeek(const api::MediaTapeSeekParams& params) {
  auto status = withDeck([&](hw::TapeControl& deck) {
    if (!deck.loaded()) throw shared::CoreError(api::ERROR_NO_TAPE, "磁带机里没有磁带");
    if (deck.mode() == hw::TapeMode::Recording) throw shared::CoreError(api::ERROR_TAPE_BUSY, "磁带机正在录音，不能定位");
    deck.seek(params.position);
  });
  publishTapeChanged();
  return api::MediaTapeSeekResult{std::move(status)};
}

api::MediaTapeTimelineResult MediaManager::tapeTimeline() {
  std::lock_guard lock(timelineMutex_);
  return api::MediaTapeTimelineResult{timeline_};
}

}  // namespace ember::logic::media
