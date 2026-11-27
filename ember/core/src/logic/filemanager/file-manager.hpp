/*
 * ------------------------------------------------------------------------
 * 名称：file-manager.hpp
 * 说明：媒体库服务的实现：监视目录并按文件夹列出内容，列出镜像里的文件，重命名、删除、移动、新建文件夹、导入、导出与撤销，新建空白镜像。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-25
 * 备注：修改类操作的做法：读出镜像（插在软驱里的读软驱里的盘片，否则读文件）→ 在副本上用文件系统改 → 比较改动前后的字节 → 只把有差别的区间写回
 *       （软驱里的盘片经 FloppyDrive::writeFromHost，由介质服务随后写回文件；否则直接改文件）；这样机器在别处写的内容不会被覆盖；
 *       每次成功的修改记一步撤销（改动的区间与改前、改后的字节，只在内存里），撤销前要核对这些位置没有被别人改过；
 *       目录靠文件监视（FileWatchFactory::watchDirectory）发现变化，监视来了通知后由后台线程去抖动一下，再核对打开过（list）的镜像的大小与修改时间、发 filemanager.treeChanged；树由界面按文件夹读取（tree），不在核心里缓存，也不读文件内容；
 *       模拟线程上的访问（找软驱里的盘片、写盘片）经 RuntimeService::runOnSimulation；
 *       锁的顺序：editMutex_（一次修改的全程）→ stateMutex_（目录、打开过的镜像戳与撤销记录，持有时不做文件读写）；通知后台线程用单独的 signalMutex_，文件监视的回调只碰它。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "contracts/filemanager/disk-file-system.hpp"
#include "contracts/filemanager/file-manager-storage.hpp"
#include "contracts/filemanager/filemanager-service.hpp"
#include "contracts/hardware/floppy-drive.hpp"
#include "contracts/runtime/runtime-service.hpp"
#include "platform/events/event-sink.hpp"
#include "platform/files/file-watcher.hpp"
#include "platform/logging/error-log.hpp"

namespace ember::logic::filemanager {

/** 媒体库服务。 */
class FileManager final : public contracts::filemanager::FileManagerService {
 public:
  /**
   * @param storage 目录里的文件存取。
   * @param watchFactory 监视目录的工厂；为空或不支持时目录变化不会自动同步。
   * @param runtime 运行服务：在模拟线程上访问软驱。
   * @param drives 软驱（按编号排列），与介质服务共用。
   * @param decoder 位流格式（.nib、.woz）的解码器；为空时这两种镜像认不出。
   * @param events 事件发布。
   * @param errors 错误日志，可以为空。
   */
  FileManager(std::shared_ptr<contracts::filemanager::FileManagerStorage> storage, std::shared_ptr<platform::FileWatchFactory> watchFactory, std::shared_ptr<contracts::runtime::RuntimeService> runtime,
                   std::vector<std::shared_ptr<contracts::hardware::FloppyDrive>> drives, std::shared_ptr<contracts::filemanager::AppleBitstreamDecoder> decoder, std::shared_ptr<platform::EventSink> events,
                   std::shared_ptr<platform::ErrorLog> errors);
  ~FileManager() override;

  FileManager(const FileManager&) = delete;
  FileManager& operator=(const FileManager&) = delete;

  /** 停止后台线程与目录监视；核心停止时调用，可重复调用。 */
  void shutdown();

  contracts::filemanager::FilemanagerSetRootResult setRoot(const contracts::filemanager::FilemanagerSetRootParams& params) override;
  [[nodiscard]] contracts::filemanager::FilemanagerTreeResult tree(const contracts::filemanager::FilemanagerTreeParams& params) override;
  [[nodiscard]] contracts::filemanager::FilemanagerListResult list(const contracts::filemanager::FilemanagerListParams& params) override;
  contracts::filemanager::FilemanagerRenameResult rename(const contracts::filemanager::FilemanagerRenameParams& params) override;
  contracts::filemanager::FilemanagerRemoveResult remove(const contracts::filemanager::FilemanagerRemoveParams& params) override;
  contracts::filemanager::FilemanagerMoveResult move(const contracts::filemanager::FilemanagerMoveParams& params) override;
  contracts::filemanager::FilemanagerCopyResult copy(const contracts::filemanager::FilemanagerCopyParams& params) override;
  contracts::filemanager::FilemanagerMakeDirectoryResult makeDirectory(const contracts::filemanager::FilemanagerMakeDirectoryParams& params) override;
  contracts::filemanager::FilemanagerUndoResult undo(const contracts::filemanager::FilemanagerUndoParams& params) override;
  [[nodiscard]] contracts::filemanager::FilemanagerReadEntriesResult readEntries(const contracts::filemanager::FilemanagerReadEntriesParams& params) override;
  contracts::filemanager::FilemanagerImportFilesResult importFiles(const contracts::filemanager::FilemanagerImportFilesParams& params) override;
  contracts::filemanager::FilemanagerCreateImageResult createImage(const contracts::filemanager::FilemanagerCreateImageParams& params) override;
  contracts::filemanager::FilemanagerRenameItemResult renameItem(const contracts::filemanager::FilemanagerRenameItemParams& params) override;
  contracts::filemanager::FilemanagerRemoveItemsResult removeItems(const contracts::filemanager::FilemanagerRemoveItemsParams& params) override;
  contracts::filemanager::FilemanagerMoveItemsResult moveItems(const contracts::filemanager::FilemanagerMoveItemsParams& params) override;
  contracts::filemanager::FilemanagerCopyItemsResult copyItems(const contracts::filemanager::FilemanagerCopyItemsParams& params) override;
  contracts::filemanager::FilemanagerMakeFolderResult makeFolder(const contracts::filemanager::FilemanagerMakeFolderParams& params) override;
  [[nodiscard]] contracts::filemanager::FilemanagerBlankKindsResult blankKinds() override;
  [[nodiscard]] contracts::filemanager::FilemanagerImagePathResult imagePath(const contracts::filemanager::FilemanagerImagePathParams& params) override;

 private:
  /** 镜像此刻的内容与它的来源。 */
  struct Source {
    std::vector<std::uint8_t> bytes;
    /** 插在哪个软驱里；没有插入为 -1。 */
    std::int64_t drive{-1};
    bool writeProtected{false};
    /** 盘片被故障注入暂时取出（镜像还在）。 */
    bool suspended{false};
  };

  /** 一处改动：位置与改前、改后的字节。 */
  struct Change {
    std::size_t offset{0};
    std::vector<std::uint8_t> before;
    std::vector<std::uint8_t> after;
  };

  /** 一步可撤销的操作。 */
  struct UndoStep {
    std::string description;
    std::vector<Change> changes;
    std::size_t bytes{0};
  };

  [[nodiscard]] std::string currentRoot();
  [[nodiscard]] Source loadSource(const std::string& root, const std::string& image);
  /** 读出镜像并认出文件系统；认不出时抛出 filemanager.unknownFormat。 */
  [[nodiscard]] std::unique_ptr<contracts::filemanager::DiskFileSystem> openFileSystem(std::vector<std::uint8_t>& bytes, const std::string& image);
  /** 对镜像做一次修改：在副本上执行 operation，写回有差别的区间，记撤销，发事件。 */
  void apply(const std::string& image, const std::string& description, const std::function<void(contracts::filemanager::DiskFileSystem&)>& operation);
  void writeChanges(const std::string& root, const std::string& image, const Source& source, const std::vector<Change>& changes, bool restore);
  void publishImageChanged(const std::string& image);
  /** 镜像或文件夹里有镜像插在软驱里时抛出 filemanager.inUse。 */
  void requireNotInDrive(const std::string& root, const std::string& relative);
  /** 丢弃一个路径（及其下的镜像）的撤销记录：文件换了位置或没了。 */
  void forgetUndo(const std::string& path);
  void publishTreeChanged();
  void logError(const std::string& code, const std::string& message, const std::string& image);

  /** 监视来了通知后的刷新：核对打开过的镜像的戳，发树与镜像变了的事件。 */
  void requestScan();
  void scanLoop();
  void runRefresh(const std::string& root);

  std::shared_ptr<contracts::filemanager::FileManagerStorage> storage_;
  std::shared_ptr<platform::FileWatchFactory> watchFactory_;
  std::shared_ptr<contracts::runtime::RuntimeService> runtime_;
  std::vector<std::shared_ptr<contracts::hardware::FloppyDrive>> drives_;
  std::shared_ptr<contracts::filemanager::AppleBitstreamDecoder> decoder_;
  std::shared_ptr<platform::EventSink> events_;
  std::shared_ptr<platform::ErrorLog> errors_;

  /** 一次修改的全程（读、改、写回、记撤销）。 */
  std::mutex editMutex_;
  /** 目录、打开过的镜像戳与撤销记录。 */
  std::mutex stateMutex_;
  std::string root_;
  /** 打开过（list）的镜像文件的（大小，修改时间），用来发现被别的程序改动的镜像。 */
  std::map<std::string, std::pair<std::uint64_t, std::int64_t>> openedStamps_;
  std::map<std::string, std::vector<UndoStep>> undo_;
  std::size_t undoBytes_{0};
  std::unique_ptr<platform::FileWatcher> watcher_;

  /** 通知后台线程：扫描请求与停止。 */
  std::mutex signalMutex_;
  std::condition_variable wake_;
  bool scanRequested_{false};
  bool stopping_{false};
  std::thread scanThread_;
};

}  // namespace ember::logic::filemanager
