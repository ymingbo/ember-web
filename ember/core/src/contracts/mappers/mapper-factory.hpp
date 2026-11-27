/*
 * ------------------------------------------------------------------------
 * 名称：mapper-factory.hpp
 * 说明：mapper 契约：按卡带描述匹配插件，并用匹配到的插件创建卡带。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * 备注：分两步：先匹配得到插件与主机型号，调用方据此装配主机；再把主机卡槽上的 IRQ 引脚交给 create 创建卡带。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "contracts/hardware/button-input.hpp"
#include "contracts/hardware/irq-output.hpp"
#include "contracts/mappers/cartridge-image.hpp"
#include "contracts/mappers/mapper-cartridge.hpp"

namespace ember::contracts::mappers {

/** 匹配结果。 */
struct MapperMatch {
  /** 匹配到的插件标识。 */
  std::string pluginId;
  /** 插件显示名称。 */
  std::string pluginName;
  /** 插件版本。 */
  std::string version;
  /** 要装配的主机型号：插件 host 里第一个已有的型号。 */
  std::string host;
  /** 插件声明的外设（目前只有 mouse）。 */
  std::vector<std::string> devices;
};

/** 卡带工厂。 */
class MapperFactory {
 public:
  virtual ~MapperFactory() = default;

  /**
   * 按描述匹配已启用的插件：写出条件更多的优先。
   * @throws shared::CoreError mappers.noMatch：没有匹配；mappers.conflict：条件数相同的插件不止一个；mappers.hostUnknown：插件要求的主机型号都不存在。
   */
  [[nodiscard]] virtual MapperMatch match(const CartridgeDescription& description) const = 0;

  /**
   * 用匹配到的插件创建卡带：按清单创建存储区、窗口与译码表，加载脚本。
   * @param match match 的结果。
   * @param image 卡带映像，数据被复制，调用后可释放。
   * @param irq 主机卡槽上的 IRQ 引脚，必须活到卡带销毁。
   * @param pads 主机上的手柄口，脚本经 ctx.pad 设置按键，必须活到卡带销毁。
   * @throws shared::CoreError mappers.notFound：插件已被卸载；mappers.invalidPackage、mappers.limitExceeded：脚本加载失败。
   */
  [[nodiscard]] virtual std::unique_ptr<MapperCartridge> create(const MapperMatch& match, const CartridgeImage& image, hardware::IrqOutput& irq, hardware::PadSink& pads) = 0;
};

}  // namespace ember::contracts::mappers
