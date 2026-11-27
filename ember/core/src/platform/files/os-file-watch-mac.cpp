/*
 * ------------------------------------------------------------------------
 * 名称：os-file-watch-mac.cpp
 * 说明：文件监视的 macOS 实现：用 kqueue 同时监视文件本身与它所在的目录，文件被写入、改名、删除或目录里的条目变化时回调。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-16
 * 备注：别的程序“先写临时文件再改名覆盖”后旧的文件描述符指向已被替换的文件，所以每次通知后重新打开文件并重新登记；停止时用 EVFILT_USER 唤醒监视线程。
 *       本文件写好但没有在 macOS 上编译与测试（项目约定先在 Windows 上开发）。
 * ------------------------------------------------------------------------
 */

#include <fcntl.h>
#include <sys/event.h>
#include <unistd.h>

#include <filesystem>
#include <thread>

#include "platform/files/os-file-watch.hpp"

namespace ember::platform {

namespace {

/** 文件与目录上关心的变化。 */
constexpr unsigned NOTE_FLAGS = NOTE_WRITE | NOTE_EXTEND | NOTE_DELETE | NOTE_RENAME | NOTE_ATTRIB | NOTE_LINK;
/** 停止事件的标识。 */
constexpr uintptr_t STOP_IDENT = 1;

/** 监视一个文件与它所在的目录。 */
class MacFileWatcher final : public FileWatcher {
 public:
  MacFileWatcher(const std::filesystem::path& file, std::function<void()> onChanged) : file_(file), onChanged_(std::move(onChanged)) {
    queue_ = kqueue();
    std::filesystem::path directory = file.parent_path();
    if (directory.empty()) directory = ".";
    directoryFd_ = open(directory.c_str(), O_EVTONLY);
    if (queue_ < 0 || directoryFd_ < 0) return;
    struct kevent change;
    EV_SET(&change, STOP_IDENT, EVFILT_USER, EV_ADD | EV_CLEAR, 0, 0, nullptr);
    kevent(queue_, &change, 1, nullptr, 0, nullptr);
    EV_SET(&change, static_cast<uintptr_t>(directoryFd_), EVFILT_VNODE, EV_ADD | EV_CLEAR, NOTE_FLAGS, 0, nullptr);
    kevent(queue_, &change, 1, nullptr, 0, nullptr);
    watchFile();
    ok_ = true;
    thread_ = std::thread([this] { run(); });
  }

  ~MacFileWatcher() override {
    if (thread_.joinable()) {
      struct kevent trigger;
      EV_SET(&trigger, STOP_IDENT, EVFILT_USER, 0, NOTE_TRIGGER, 0, nullptr);
      kevent(queue_, &trigger, 1, nullptr, 0, nullptr);
      thread_.join();
    }
    if (fileFd_ >= 0) close(fileFd_);
    if (directoryFd_ >= 0) close(directoryFd_);
    if (queue_ >= 0) close(queue_);
  }

  [[nodiscard]] bool ok() const { return ok_; }

 private:
  /** 重新打开文件并登记；文件不存在时只靠目录的通知。 */
  void watchFile() {
    if (fileFd_ >= 0) close(fileFd_);
    fileFd_ = open(file_.c_str(), O_EVTONLY);
    if (fileFd_ < 0) return;
    struct kevent change;
    EV_SET(&change, static_cast<uintptr_t>(fileFd_), EVFILT_VNODE, EV_ADD | EV_CLEAR, NOTE_FLAGS, 0, nullptr);
    kevent(queue_, &change, 1, nullptr, 0, nullptr);
  }

  void run() {
    while (true) {
      struct kevent event;
      if (kevent(queue_, nullptr, 0, &event, 1, nullptr) <= 0) return;
      if (event.filter == EVFILT_USER) return;
      watchFile();
      onChanged_();
    }
  }

  std::filesystem::path file_;
  std::function<void()> onChanged_;
  int queue_{-1};
  int directoryFd_{-1};
  int fileFd_{-1};
  bool ok_{false};
  std::thread thread_;
};

/** macOS 的文件监视工厂。 */
class MacFileWatchFactory final : public FileWatchFactory {
 public:
  [[nodiscard]] std::unique_ptr<FileWatcher> watch(const std::string& path, std::function<void()> onChanged) override {
    auto watcher = std::make_unique<MacFileWatcher>(std::filesystem::path(std::u8string(path.begin(), path.end())), std::move(onChanged));
    if (!watcher->ok()) return nullptr;
    return watcher;
  }
};

}  // namespace

std::shared_ptr<FileWatchFactory> createOsFileWatchFactory() { return std::make_shared<MacFileWatchFactory>(); }

}  // namespace ember::platform
