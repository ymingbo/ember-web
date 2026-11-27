/*
 * ------------------------------------------------------------------------
 * 名称：media-requests.cpp
 * 说明：media 模块的请求登记实现：校验参数，调用介质服务，返回结果。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-15
 * ------------------------------------------------------------------------
 */

#include "requests/media/media-requests.hpp"

namespace ember::requests::media {

namespace contract = ::ember::contracts::media;

void addMediaRequests(platform::RequestRouter& router, std::shared_ptr<contract::MediaService> service) {
  router.add("media", contract::MEDIA_FLOPPY_INSERT_REQUEST, [service](const nlohmann::json& params, const platform::CancellationToken&) {
    return nlohmann::json(service->floppyInsert(contract::parseMediaFloppyInsertParams(params, "params")));
  });
  router.add("media", contract::MEDIA_FLOPPY_EJECT_REQUEST, [service](const nlohmann::json& params, const platform::CancellationToken&) {
    return nlohmann::json(service->floppyEject(contract::parseMediaFloppyEjectParams(params, "params")));
  });
  router.add("media", contract::MEDIA_FLOPPY_SAVE_REQUEST, [service](const nlohmann::json& params, const platform::CancellationToken&) {
    return nlohmann::json(service->floppySave(contract::parseMediaFloppySaveParams(params, "params")));
  });
  router.add("media", contract::MEDIA_FLOPPY_WRITE_PROTECT_REQUEST, [service](const nlohmann::json& params, const platform::CancellationToken&) {
    return nlohmann::json(service->floppyWriteProtect(contract::parseMediaFloppyWriteProtectParams(params, "params")));
  });
  router.add("media", contract::MEDIA_FLOPPY_STATUS_REQUEST, [service](const nlohmann::json& params, const platform::CancellationToken&) {
    static_cast<void>(contract::parseMediaFloppyStatusParams(params, "params"));
    return nlohmann::json(service->floppyStatus());
  });
  router.add("media", contract::MEDIA_FLOPPY_FAULT_REQUEST, [service](const nlohmann::json& params, const platform::CancellationToken&) {
    return nlohmann::json(service->floppyFault(contract::parseMediaFloppyFaultParams(params, "params")));
  });
  router.add("media", contract::MEDIA_PRINTER_STATUS_REQUEST, [service](const nlohmann::json& params, const platform::CancellationToken&) {
    static_cast<void>(contract::parseMediaPrinterStatusParams(params, "params"));
    return nlohmann::json(service->printerStatus());
  });
  router.add("media", contract::MEDIA_PRINTER_FLUSH_REQUEST, [service](const nlohmann::json& params, const platform::CancellationToken&) {
    static_cast<void>(contract::parseMediaPrinterFlushParams(params, "params"));
    return nlohmann::json(service->printerFlush());
  });
  router.add("media", contract::MEDIA_TAPE_LOAD_REQUEST, [service](const nlohmann::json& params, const platform::CancellationToken&) {
    return nlohmann::json(service->tapeLoad(contract::parseMediaTapeLoadParams(params, "params")));
  });
  router.add("media", contract::MEDIA_TAPE_EJECT_REQUEST, [service](const nlohmann::json& params, const platform::CancellationToken&) {
    static_cast<void>(contract::parseMediaTapeEjectParams(params, "params"));
    return nlohmann::json(service->tapeEject());
  });
  router.add("media", contract::MEDIA_TAPE_PLAY_REQUEST, [service](const nlohmann::json& params, const platform::CancellationToken&) {
    static_cast<void>(contract::parseMediaTapePlayParams(params, "params"));
    return nlohmann::json(service->tapePlay());
  });
  router.add("media", contract::MEDIA_TAPE_RECORD_REQUEST, [service](const nlohmann::json& params, const platform::CancellationToken&) {
    return nlohmann::json(service->tapeRecord(contract::parseMediaTapeRecordParams(params, "params")));
  });
  router.add("media", contract::MEDIA_TAPE_STOP_REQUEST, [service](const nlohmann::json& params, const platform::CancellationToken&) {
    static_cast<void>(contract::parseMediaTapeStopParams(params, "params"));
    return nlohmann::json(service->tapeStop());
  });
  router.add("media", contract::MEDIA_TAPE_STATUS_REQUEST, [service](const nlohmann::json& params, const platform::CancellationToken&) {
    static_cast<void>(contract::parseMediaTapeStatusParams(params, "params"));
    return nlohmann::json(service->tapeStatus());
  });
  router.add("media", contract::MEDIA_TAPE_SEEK_REQUEST, [service](const nlohmann::json& params, const platform::CancellationToken&) {
    return nlohmann::json(service->tapeSeek(contract::parseMediaTapeSeekParams(params, "params")));
  });
  router.add("media", contract::MEDIA_TAPE_TIMELINE_REQUEST, [service](const nlohmann::json& params, const platform::CancellationToken&) {
    static_cast<void>(contract::parseMediaTapeTimelineParams(params, "params"));
    return nlohmann::json(service->tapeTimeline());
  });
}

}  // namespace ember::requests::media
