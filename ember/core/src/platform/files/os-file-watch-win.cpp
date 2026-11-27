/*
 * ------------------------------------------------------------------------
 * 名称：os-file-watch-win.cpp
 * 说明：文件监视的 Windows 实现：在文件所在的目录上用 ReadDirectoryChangesW（重叠 I/O）等变化通知，名字与被监视的文件相同的条目才回调；监视整个目录时连同子目录里的任何变化都回调。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-16
 * 备注：监视目录而不是文件本身，所以别的程序“先写临时文件再改名覆盖”的保存方式也能收到；通知缓冲区溢出（丢了通知）时也回调一次；
 *       停止时置停止事件并取消未完成的读取，再等线程结束。
 * ------------------------------------------------------------------------
 */

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <array>
#include <cwchar>
#include <filesystem>
#include <thread>

#include "platform/files/os-file-watch.hpp"

namespace ember::platform {

namespace {

/** 通知缓冲区的大小（字节）。 */
constexpr DWORD BUFFER_SIZE = 16 * 1024;

/** 自动关闭的句柄。 */
class Handle {
 public:
  Handle() = default;
  ~Handle() { reset(); }
  Handle(const Handle&) = delete;
  Handle& operator=(const Handle&) = delete;

  [[nodiscard]] HANDLE get() const { return handle_; }
  [[nodiscard]] bool valid() const { return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE; }

  /** 换成另一个句柄，原来的关闭。 */
  void set(HANDLE handle) {
    reset();
    handle_ = handle;
  }

  void reset() {
    if (valid()) CloseHandle(handle_);
    handle_ = nullptr;
  }

 private:
  HANDLE handle_ = nullptr;
};

/** 监视一个文件所在的目录，或整个目录（含子目录）。 */
class WindowsFileWatcher final : public FileWatcher {
 public:
  /**
   * 打开目录并启动监视线程；失败时 ok() 为 false。
   * @param target 要监视的文件；wholeDirectory 为真时是要监视的目录。
   * @param wholeDirectory 为真时监视目录里（含子目录）任何文件与目录的变化，不看名字。
   */
  WindowsFileWatcher(const std::filesystem::path& target, bool wholeDirectory, std::function<void()> onChanged)
      : name_(wholeDirectory ? std::wstring() : target.filename().wstring()), subtree_(wholeDirectory), onChanged_(std::move(onChanged)) {
    std::filesystem::path directory = wholeDirectory ? target : target.parent_path();
    if (directory.empty()) directory = ".";
    directory_.set(CreateFileW(directory.c_str(), FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                               FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr));
    changeEvent_.set(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    stopEvent_.set(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!directory_.valid() || !changeEvent_.valid() || !stopEvent_.valid()) return;
    // 第一次读取在这里就发出，之后目录里的变化都会被记下，不会因为线程还没跑起来而漏掉。
    if (!arm()) return;
    ok_ = true;
    thread_ = std::thread([this] { run(); });
  }

  ~WindowsFileWatcher() override {
    if (!thread_.joinable()) return;
    SetEvent(stopEvent_.get());
    thread_.join();
  }

  [[nodiscard]] bool ok() const { return ok_; }

 private:
  /** 发出一次目录变化的读取（重叠 I/O，完成时置 changeEvent_）。 */
  bool arm() {
    overlapped_ = {};
    overlapped_.hEvent = changeEvent_.get();
    ResetEvent(changeEvent_.get());
    const DWORD filter = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE | (subtree_ ? FILE_NOTIFY_CHANGE_DIR_NAME : 0);
    return ReadDirectoryChangesW(directory_.get(), buffer_.data(), BUFFER_SIZE, subtree_ ? TRUE : FALSE, filter, nullptr, &overlapped_, nullptr) != FALSE;
  }

  /** 等通知，直到被要求停止。 */
  void run() {
    const HANDLE waits[2] = {changeEvent_.get(), stopEvent_.get()};
    while (true) {
      if (WaitForMultipleObjects(2, waits, FALSE, INFINITE) != WAIT_OBJECT_0) {
        CancelIoEx(directory_.get(), &overlapped_);
        // 取消的读取要等它结束，缓冲区才能释放。
        DWORD ignored = 0;
        GetOverlappedResult(directory_.get(), &overlapped_, &ignored, TRUE);
        return;
      }
      DWORD bytes = 0;
      if (!GetOverlappedResult(directory_.get(), &overlapped_, &bytes, FALSE)) return;
      // 先重新发出读取再处理这一批：处理期间的变化留在系统的缓冲区里。
      const auto batch = buffer_;
      if (!arm()) return;
      // 返回 0 字节表示通知缓冲区溢出，不知道丢了什么，按有变化处理。
      if (bytes == 0 || touchesFile(reinterpret_cast<const char*>(batch.data()))) onChanged_();
    }
  }

  /** 这一批通知里有没有被监视的文件；监视整个目录时任何通知都算。 */
  [[nodiscard]] bool touchesFile(const char* buffer) const {
    if (subtree_) return true;
    const char* cursor = buffer;
    while (true) {
      const auto* entry = reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(cursor);
      const std::size_t length = entry->FileNameLength / sizeof(wchar_t);
      if (length == name_.size() && _wcsnicmp(entry->FileName, name_.c_str(), length) == 0) return true;
      if (entry->NextEntryOffset == 0) return false;
      cursor += entry->NextEntryOffset;
    }
  }

  std::wstring name_;
  bool subtree_;
  std::function<void()> onChanged_;
  Handle directory_;
  Handle changeEvent_;
  Handle stopEvent_;
  bool ok_{false};
  /** 通知缓冲区：用 DWORD 数组保证 4 字节对齐。 */
  std::array<DWORD, BUFFER_SIZE / sizeof(DWORD)> buffer_{};
  OVERLAPPED overlapped_{};
  std::thread thread_;
};

/** Windows 的文件监视工厂。 */
class WindowsFileWatchFactory final : public FileWatchFactory {
 public:
  [[nodiscard]] std::unique_ptr<FileWatcher> watch(const std::string& path, std::function<void()> onChanged) override {
    auto watcher = std::make_unique<WindowsFileWatcher>(std::filesystem::path(std::u8string(path.begin(), path.end())), false, std::move(onChanged));
    if (!watcher->ok()) return nullptr;
    return watcher;
  }

  [[nodiscard]] std::unique_ptr<FileWatcher> watchDirectory(const std::string& path, std::function<void()> onChanged) override {
    auto watcher = std::make_unique<WindowsFileWatcher>(std::filesystem::path(std::u8string(path.begin(), path.end())), true, std::move(onChanged));
    if (!watcher->ok()) return nullptr;
    return watcher;
  }
};

}  // namespace

std::shared_ptr<FileWatchFactory> createOsFileWatchFactory() { return std::make_shared<WindowsFileWatchFactory>(); }

}  // namespace ember::platform
