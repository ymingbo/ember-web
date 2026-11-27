/*
 * ------------------------------------------------------------------------
 * 名称：trash-bin-win.cpp
 * 说明：回收站的 Windows 实现：用 SHFileOperationW 的删除并允许撤销（FOF_ALLOWUNDO），把文件和文件夹移进回收站。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-26
 * 备注：SHFileOperationW 要求完整路径、反斜杠分隔、以两个空字符结尾；不弹确认与错误窗口。
 * ------------------------------------------------------------------------
 */

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>

#include <string>

#include "platform/files/trash-bin.hpp"

namespace ember::platform {

namespace {

/** UTF-8 转宽字符；失败时返回空串。 */
std::wstring toWide(const std::string& text) {
  if (text.empty()) return {};
  const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
  if (length <= 0) return {};
  std::wstring wide(static_cast<std::size_t>(length), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), length);
  return wide;
}

class WindowsTrashBin final : public TrashBin {
 public:
  bool moveToTrash(const std::vector<std::string>& paths) override {
    if (paths.empty()) return true;
    std::wstring list;
    for (const std::string& path : paths) {
      std::wstring wide = toWide(path);
      if (wide.empty()) return false;
      for (wchar_t& c : wide) {
        if (c == L'/') c = L'\\';
      }
      list += wide;
      list.push_back(L'\0');
    }
    list.push_back(L'\0');
    SHFILEOPSTRUCTW operation{};
    operation.wFunc = FO_DELETE;
    operation.pFrom = list.c_str();
    operation.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;
    return SHFileOperationW(&operation) == 0 && !operation.fAnyOperationsAborted;
  }
};

}  // namespace

std::shared_ptr<TrashBin> createOsTrashBin() { return std::make_shared<WindowsTrashBin>(); }

}  // namespace ember::platform
