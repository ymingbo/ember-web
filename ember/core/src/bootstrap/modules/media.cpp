/*
 * ------------------------------------------------------------------------
 * 名称：media.cpp
 * 说明：介质模块的登记：创建盘片镜像文件的磁盘存取、磁带文件的磁盘存取（FFmpeg 库解码）、打印页的 PNG 保存与介质服务，登记“打印机”分组的设置项与 media.* 请求，把打印机输出的接收方绑定到运行模块创建的转接对象上。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-15
 * 备注：取用 preferences 与 runtime 模块的服务，须排在它们之后登记；打印页的保存目录读首选项 printer.outputDirectory，为空时用用户数据目录下的 printer-output；
 *       每输出一页读取一次，所以修改后立即生效；盘片镜像的自动写回与镜像文件被外部修改的同步由介质服务订阅驱动器的修改通知与系统的文件变化通知（在这里注入）完成，核心停止时先写回再停模拟线程。
 * ------------------------------------------------------------------------
 */

#include "bootstrap/modules/media.hpp"

#include <memory>
#include <string>
#include <variant>

#include "bootstrap/machines/common/floppy-activity-relay.hpp"
#include "bootstrap/machines/common/printer-output-relay.hpp"
#include "contracts/hardware/floppy-drive.hpp"
#include "contracts/hardware/tape-deck.hpp"
#include "contracts/media/media-service.hpp"
#include "contracts/preferences/preference-service.hpp"
#include "contracts/runtime/runtime-service.hpp"
#include "infra/media/disk-floppy-image-store.hpp"
#include "infra/media/disk-tape-store.hpp"
#include "infra/media/png-printer-page-store.hpp"
#include "logic/media/media-manager.hpp"
#include "logic/media/printer-service.hpp"
#include "platform/files/os-file-watch.hpp"
#include "requests/media/media-requests.hpp"

namespace ember::bootstrap {

namespace {

void registerPrinterSettings(contracts::preferences::PreferenceService& service) {
  namespace api = contracts::preferences;
  service.addGroup({contracts::media::SETTING_PRINTER_GROUP, "打印机"});
  service.addSetting({.id = contracts::media::SETTING_PRINTER_OUTPUT_DIRECTORY,
                      .group = contracts::media::SETTING_PRINTER_GROUP,
                      .label = "图片保存位置",
                      .description = "打印机输出的每一页保存成一张 PNG 图片，文件名按日期时间自动生成；留空时保存在用户数据目录下的 printer-output。",
                      .kind = api::SettingKind::Text,
                      .maxLength = 260,
                      .picker = "directory",
                      .defaultValue = std::string()});
}

}  // namespace

void registerMedia(ModuleRegistry& registry) {
  auto preferences = registry.services.require<contracts::preferences::PreferenceService>();
  registerPrinterSettings(*preferences);

  const auto userData = registry.userDataPath;
  const auto printerDirectory = [preferences, userData] {
    const auto value = preferences->value(contracts::media::SETTING_PRINTER_OUTPUT_DIRECTORY);
    const auto* text = std::get_if<std::string>(&value);
    if (text != nullptr && !text->empty()) return *text;
    const auto fallback = (userData / "printer-output").u8string();
    return std::string(fallback.begin(), fallback.end());
  };
  auto printer = std::make_shared<logic::media::PrinterService>(std::make_shared<infra::media::PngPrinterPageStore>(), printerDirectory, registry.events, registry.errors);
  registry.services.require<PrinterOutputRelay>()->bind(printer);

  auto service = std::make_shared<logic::media::MediaManager>(std::make_shared<infra::media::DiskFloppyImageStore>(), std::make_shared<infra::media::DiskTapeStore>(registry.errors),
                                                              registry.services.require<contracts::runtime::RuntimeService>(), registry.events, printer,
                                                              registry.services.require<contracts::hardware::TapeControl>(),
                                                              std::vector<std::shared_ptr<contracts::hardware::FloppyDrive>>{registry.services.require<contracts::hardware::FloppyDrive>()},
                                                              registry.errors, platform::createOsFileWatchFactory());
  registry.services.provide<contracts::media::MediaService>(service);
  registry.services.require<FloppyActivityRelay>()->bind(service);
  requests::media::addMediaRequests(registry.requests, service);
  // 后登记的先停：本模块排在运行模块之后，所以核心停止时先把未写回的盘片修改写回，再停模拟线程。
  registry.stopHandlers.push_back([service] { service->shutdown(); });
}

}  // namespace ember::bootstrap
