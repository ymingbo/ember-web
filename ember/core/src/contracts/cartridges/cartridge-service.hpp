/*
 * ------------------------------------------------------------------------
 * 名称：cartridge-service.hpp
 * 说明：cartridges 模块的服务接口：加载 NES 文件、取出卡带、卡带信息。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * 备注：数据类型由 schema/cartridges.schema.json 生成到 generated/；请求层只经本接口使用卡带模块。
 * ------------------------------------------------------------------------
 */

#pragma once

#include "contracts/cartridges/generated/cartridges.hpp"

namespace ember::contracts::cartridges {

/** 卡带服务。 */
class CartridgeService {
 public:
  virtual ~CartridgeService() = default;

  /**
   * 读取 NES 文件，解析、匹配插件、装配主机并插入卡槽；失败时原有的卡带与主机保持不变。
   * @throws shared::CoreError cartridges.* 与 mappers.* 的错误，见 schema/cartridges.schema.json。
   */
  virtual CartridgesLoadResult load(const CartridgesLoadParams& params) = 0;

  /** 取出卡带，电源变为断电；没有卡带时没有效果。 */
  virtual CartridgesEjectResult eject() = 0;

  /**
   * 只解析文件头并预览匹配结果，不装配、不改变正在运行的卡带与主机。
   * @throws shared::CoreError cartridges.fileNotFound、cartridges.fileUnreadable、cartridges.notInes、cartridges.truncated、
   *         cartridges.unsupportedFormat、cartridges.invalidHeader；匹配失败不抄出，写在结果的 matchError 里。
   */
  virtual CartridgesParseHeaderResult parseHeader(const CartridgesParseHeaderParams& params) = 0;

  /** 当前卡带的描述、插件与存储区清单。@throws shared::CoreError cartridges.empty。 */
  [[nodiscard]] virtual CartridgesInfoResult info() const = 0;
};

}  // namespace ember::contracts::cartridges
