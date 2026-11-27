/*
 * ------------------------------------------------------------------------
 * 名称：filemanager.cpp
 * 说明：媒体库模块的登记：创建目录的磁盘存取、位流格式解码器与媒体库服务，登记 filemanager.* 请求。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-25
 * 备注：取用 runtime 模块的运行服务与软驱（与介质服务共用同一个驱动器），须排在它们之后登记；核心停止时停下目录监视与后台扫描线程。
 * ------------------------------------------------------------------------
 */

#include "bootstrap/modules/filemanager.hpp"

#include <memory>
#include <vector>

#include "bootstrap/apple-bitstream-decoder.hpp"
#include "contracts/hardware/floppy-drive.hpp"
#include "contracts/runtime/runtime-service.hpp"
#include "infra/filemanager/file-manager-storage.hpp"
#include "logic/filemanager/file-manager.hpp"
#include "platform/files/os-file-watch.hpp"
#include "platform/files/trash-bin.hpp"
#include "requests/filemanager/filemanager-requests.hpp"

namespace ember::bootstrap {

void registerFileManager(ModuleRegistry& registry) {
  auto service = std::make_shared<logic::filemanager::FileManager>(
      std::make_shared<infra::filemanager::FileManagerStorageOnDisk>(platform::createOsTrashBin()), platform::createOsFileWatchFactory(), registry.services.require<contracts::runtime::RuntimeService>(),
      std::vector<std::shared_ptr<contracts::hardware::FloppyDrive>>{registry.services.require<contracts::hardware::FloppyDrive>()}, std::make_shared<AppleBitstreamDecoder>(), registry.events,
      registry.errors);
  registry.services.provide<contracts::filemanager::FileManagerService>(service);
  requests::filemanager::addFileManagerRequests(registry.requests, service);
  registry.stopHandlers.push_back([service] { service->shutdown(); });
}

}  // namespace ember::bootstrap
