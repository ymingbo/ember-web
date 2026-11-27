/*
 * ------------------------------------------------------------------------
 * 名称：filemanager-service.hpp
 * 说明：filemanager 模块的服务接口：目录的设置与按文件夹读取的树（含软盘镜像、nes 卡带、png 图片、音频）、镜像里文件的列出、重命名、删除、移动、新建文件夹、导入、导出、撤销，空白镜像的新建。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-25
 * 备注：数据类型由 schema/filemanager.schema.json 生成到 generated/；请求层只经本接口使用 filemanager 模块；
 *       修改类操作立即生效（镜像插在软驱里时改软驱里的盘片，随后由介质服务写回文件，否则直接改文件）并发 filemanager.imageChanged，没有确认。
 * ------------------------------------------------------------------------
 */

#pragma once

#include "contracts/filemanager/generated/filemanager.hpp"

namespace ember::contracts::filemanager {

/** 媒体库服务。 */
class FileManagerService {
 public:
  virtual ~FileManagerService() = default;

  /** 设置目录并开始监视与扫描；path 为空表示取消。@throws shared::CoreError filemanager.invalidPath。 */
  virtual FilemanagerSetRootResult setRoot(const FilemanagerSetRootParams& params) = 0;

  /** 目录里一个文件夹的直接内容。@throws shared::CoreError filemanager.invalidPath。 */
  [[nodiscard]] virtual FilemanagerTreeResult tree(const FilemanagerTreeParams& params) = 0;

  /** 列出镜像里一个文件夹的内容。@throws shared::CoreError filemanager.* 的错误，见 schema/filemanager.schema.json。 */
  [[nodiscard]] virtual FilemanagerListResult list(const FilemanagerListParams& params) = 0;

  virtual FilemanagerRenameResult rename(const FilemanagerRenameParams& params) = 0;
  virtual FilemanagerRemoveResult remove(const FilemanagerRemoveParams& params) = 0;
  virtual FilemanagerMoveResult move(const FilemanagerMoveParams& params) = 0;
  virtual FilemanagerCopyResult copy(const FilemanagerCopyParams& params) = 0;
  virtual FilemanagerMakeDirectoryResult makeDirectory(const FilemanagerMakeDirectoryParams& params) = 0;
  virtual FilemanagerUndoResult undo(const FilemanagerUndoParams& params) = 0;
  [[nodiscard]] virtual FilemanagerReadEntriesResult readEntries(const FilemanagerReadEntriesParams& params) = 0;
  virtual FilemanagerImportFilesResult importFiles(const FilemanagerImportFilesParams& params) = 0;
  virtual FilemanagerCreateImageResult createImage(const FilemanagerCreateImageParams& params) = 0;
  /** 目录里文件与文件夹的操作（作用在磁盘上的文件，不是镜像内部）。 */
  virtual FilemanagerRenameItemResult renameItem(const FilemanagerRenameItemParams& params) = 0;
  virtual FilemanagerRemoveItemsResult removeItems(const FilemanagerRemoveItemsParams& params) = 0;
  virtual FilemanagerMoveItemsResult moveItems(const FilemanagerMoveItemsParams& params) = 0;
  virtual FilemanagerCopyItemsResult copyItems(const FilemanagerCopyItemsParams& params) = 0;
  virtual FilemanagerMakeFolderResult makeFolder(const FilemanagerMakeFolderParams& params) = 0;
  [[nodiscard]] virtual FilemanagerBlankKindsResult blankKinds() = 0;
  [[nodiscard]] virtual FilemanagerImagePathResult imagePath(const FilemanagerImagePathParams& params) = 0;
};

}  // namespace ember::contracts::filemanager
