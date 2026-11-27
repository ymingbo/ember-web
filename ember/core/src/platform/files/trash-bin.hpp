/*
 * ------------------------------------------------------------------------
 * 名称：trash-bin.hpp
 * 说明：回收站的系统实现入口：把文件和文件夹移进系统回收站，删错了还能找回。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-26
 * 备注：Windows 的实现在 trash-bin-win.cpp，macOS 的实现在 trash-bin-mac.cpp，只由组装根使用。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <memory>
#include <string>
#include <vector>

namespace ember::platform {

/** 系统回收站。 */
class TrashBin {
 public:
  virtual ~TrashBin() = default;

  /**
   * 把文件或文件夹移进回收站，不弹窗、不确认。
   * @param paths 完整路径，UTF-8。
   * @return 全部移走时为 true；任何一项失败为 false（失败的项留在原处）。
   */
  virtual bool moveToTrash(const std::vector<std::string>& paths) = 0;
};

/** 创建当前平台的回收站。 */
std::shared_ptr<TrashBin> createOsTrashBin();

}  // namespace ember::platform
