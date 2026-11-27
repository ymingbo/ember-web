/*
 * ------------------------------------------------------------------------
 * 名称：trash-bin-mac.cpp
 * 说明：回收站的 macOS 实现：把文件和文件夹改名移进用户的“.Trash”文件夹，重名时在名称后加序号。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-26
 * 备注：不在 Windows 上编译与测试；跨卷（改名失败）时返回 false，文件留在原处。
 * ------------------------------------------------------------------------
 */

#include <cstdlib>
#include <filesystem>

#include "platform/files/trash-bin.hpp"

namespace ember::platform {

namespace {

namespace fs = std::filesystem;

class MacTrashBin final : public TrashBin {
 public:
  bool moveToTrash(const std::vector<std::string>& paths) override {
    const char* home = std::getenv("HOME");
    if (home == nullptr) return false;
    const fs::path trash = fs::path(home) / ".Trash";
    std::error_code error;
    bool all = true;
    for (const std::string& text : paths) {
      const fs::path source(text);
      fs::path target = trash / source.filename();
      for (int number = 2; fs::exists(target, error) && number < 10000; ++number) {
        target = trash / (source.stem().string() + " " + std::to_string(number) + source.extension().string());
      }
      fs::rename(source, target, error);
      if (error) all = false;
    }
    return all;
  }
};

}  // namespace

std::shared_ptr<TrashBin> createOsTrashBin() { return std::make_shared<MacTrashBin>(); }

}  // namespace ember::platform
