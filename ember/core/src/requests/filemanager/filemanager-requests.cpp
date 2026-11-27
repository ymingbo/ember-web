/*
 * ------------------------------------------------------------------------
 * 名称：filemanager-requests.cpp
 * 说明：filemanager 模块的请求登记实现：校验参数，调用媒体库服务，返回结果。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-25
 * ------------------------------------------------------------------------
 */

#include "requests/filemanager/filemanager-requests.hpp"

namespace ember::requests::filemanager {

namespace contract = ::ember::contracts::filemanager;

void addFileManagerRequests(platform::RequestRouter& router, std::shared_ptr<contract::FileManagerService> service) {
  router.add("filemanager", contract::FILEMANAGER_SET_ROOT_REQUEST, [service](const nlohmann::json& params, const platform::CancellationToken&) {
    return nlohmann::json(service->setRoot(contract::parseFilemanagerSetRootParams(params, "params")));
  });
  router.add("filemanager", contract::FILEMANAGER_TREE_REQUEST, [service](const nlohmann::json& params, const platform::CancellationToken&) {
    return nlohmann::json(service->tree(contract::parseFilemanagerTreeParams(params, "params")));
  });
  router.add("filemanager", contract::FILEMANAGER_LIST_REQUEST, [service](const nlohmann::json& params, const platform::CancellationToken&) {
    return nlohmann::json(service->list(contract::parseFilemanagerListParams(params, "params")));
  });
  router.add("filemanager", contract::FILEMANAGER_RENAME_REQUEST, [service](const nlohmann::json& params, const platform::CancellationToken&) {
    return nlohmann::json(service->rename(contract::parseFilemanagerRenameParams(params, "params")));
  });
  router.add("filemanager", contract::FILEMANAGER_REMOVE_REQUEST, [service](const nlohmann::json& params, const platform::CancellationToken&) {
    return nlohmann::json(service->remove(contract::parseFilemanagerRemoveParams(params, "params")));
  });
  router.add("filemanager", contract::FILEMANAGER_MOVE_REQUEST, [service](const nlohmann::json& params, const platform::CancellationToken&) {
    return nlohmann::json(service->move(contract::parseFilemanagerMoveParams(params, "params")));
  });
  router.add("filemanager", contract::FILEMANAGER_COPY_REQUEST, [service](const nlohmann::json& params, const platform::CancellationToken&) {
    return nlohmann::json(service->copy(contract::parseFilemanagerCopyParams(params, "params")));
  });
  router.add("filemanager", contract::FILEMANAGER_MAKE_DIRECTORY_REQUEST, [service](const nlohmann::json& params, const platform::CancellationToken&) {
    return nlohmann::json(service->makeDirectory(contract::parseFilemanagerMakeDirectoryParams(params, "params")));
  });
  router.add("filemanager", contract::FILEMANAGER_UNDO_REQUEST, [service](const nlohmann::json& params, const platform::CancellationToken&) {
    return nlohmann::json(service->undo(contract::parseFilemanagerUndoParams(params, "params")));
  });
  router.add("filemanager", contract::FILEMANAGER_READ_ENTRIES_REQUEST, [service](const nlohmann::json& params, const platform::CancellationToken&) {
    return nlohmann::json(service->readEntries(contract::parseFilemanagerReadEntriesParams(params, "params")));
  });
  router.add("filemanager", contract::FILEMANAGER_IMPORT_FILES_REQUEST, [service](const nlohmann::json& params, const platform::CancellationToken&) {
    return nlohmann::json(service->importFiles(contract::parseFilemanagerImportFilesParams(params, "params")));
  });
  router.add("filemanager", contract::FILEMANAGER_CREATE_IMAGE_REQUEST, [service](const nlohmann::json& params, const platform::CancellationToken&) {
    return nlohmann::json(service->createImage(contract::parseFilemanagerCreateImageParams(params, "params")));
  });
  router.add("filemanager", contract::FILEMANAGER_RENAME_ITEM_REQUEST, [service](const nlohmann::json& params, const platform::CancellationToken&) {
    return nlohmann::json(service->renameItem(contract::parseFilemanagerRenameItemParams(params, "params")));
  });
  router.add("filemanager", contract::FILEMANAGER_REMOVE_ITEMS_REQUEST, [service](const nlohmann::json& params, const platform::CancellationToken&) {
    return nlohmann::json(service->removeItems(contract::parseFilemanagerRemoveItemsParams(params, "params")));
  });
  router.add("filemanager", contract::FILEMANAGER_MOVE_ITEMS_REQUEST, [service](const nlohmann::json& params, const platform::CancellationToken&) {
    return nlohmann::json(service->moveItems(contract::parseFilemanagerMoveItemsParams(params, "params")));
  });
  router.add("filemanager", contract::FILEMANAGER_COPY_ITEMS_REQUEST, [service](const nlohmann::json& params, const platform::CancellationToken&) {
    return nlohmann::json(service->copyItems(contract::parseFilemanagerCopyItemsParams(params, "params")));
  });
  router.add("filemanager", contract::FILEMANAGER_MAKE_FOLDER_REQUEST, [service](const nlohmann::json& params, const platform::CancellationToken&) {
    return nlohmann::json(service->makeFolder(contract::parseFilemanagerMakeFolderParams(params, "params")));
  });
  router.add("filemanager", contract::FILEMANAGER_BLANK_KINDS_REQUEST, [service](const nlohmann::json& params, const platform::CancellationToken&) {
    static_cast<void>(contract::parseFilemanagerBlankKindsParams(params, "params"));
    return nlohmann::json(service->blankKinds());
  });
  router.add("filemanager", contract::FILEMANAGER_IMAGE_PATH_REQUEST, [service](const nlohmann::json& params, const platform::CancellationToken&) {
    return nlohmann::json(service->imagePath(contract::parseFilemanagerImagePathParams(params, "params")));
  });
}

}  // namespace ember::requests::filemanager
