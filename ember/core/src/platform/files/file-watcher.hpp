/*
 * ------------------------------------------------------------------------
 * 名称：file-watcher.hpp
 * 说明：平台抽象：监视一个文件，或监视一个目录（含子目录）的变化。文件被修改、改名覆盖或删除时，目录里有条目增删、改名或写入时，在监视线程上调用一次回调。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-16
 * 备注：靠系统的文件变化通知（Windows 的 ReadDirectoryChangesW、macOS 的 kqueue），不轮询；
 *       回调在监视线程上调用，必须很快返回，不能阻塞；一次修改可能触发多次回调，调用方自己合并；
 *       FileWatcher 销毁时停止监视并等监视线程结束，之后不再有回调；实现见 os-file-watch.hpp。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <functional>
#include <memory>
#include <string>

namespace ember::platform {

/** 一个正在进行的文件监视；销毁即停止。 */
class FileWatcher {
 public:
  virtual ~FileWatcher() = default;
};

/** 创建文件监视。 */
class FileWatchFactory {
 public:
  virtual ~FileWatchFactory() = default;

  /**
   * 开始监视一个文件。
   * @param path 文件路径（UTF-8）。
   * @param onChanged 文件变化时在监视线程上调用。
   * @return 监视；无法监视（目录不存在、没有权限等）时为空。
   */
  [[nodiscard]] virtual std::unique_ptr<FileWatcher> watch(const std::string& path, std::function<void()> onChanged) = 0;

  /**
   * 开始监视一个目录：目录里（含子目录）的文件与目录被创建、删除、改名或写入时在监视线程上调用，回调不带路径，调用方自己重新扫描。
   * 默认不支持（返回空）。
   * @param path 目录路径（UTF-8）。
   * @return 监视；无法监视（目录不存在、没有权限、平台不支持）时为空。
   */
  [[nodiscard]] virtual std::unique_ptr<FileWatcher> watchDirectory(const std::string& path, std::function<void()> onChanged) {
    (void)path;
    (void)onChanged;
    return nullptr;
  }
};

}  // namespace ember::platform
