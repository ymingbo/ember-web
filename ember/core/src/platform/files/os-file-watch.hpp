/*
 * ------------------------------------------------------------------------
 * 名称：os-file-watch.hpp
 * 说明：文件监视的系统实现入口：创建当前平台的 FileWatchFactory。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-16
 * 备注：Windows 的实现在 os-file-watch-win.cpp，macOS 的实现在 os-file-watch-mac.cpp，只由组装根使用。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <memory>

#include "platform/files/file-watcher.hpp"

namespace ember::platform {

/** 创建当前平台的文件监视工厂。 */
std::shared_ptr<FileWatchFactory> createOsFileWatchFactory();

}  // namespace ember::platform
