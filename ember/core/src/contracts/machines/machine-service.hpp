/*
 * ------------------------------------------------------------------------
 * 名称：machine-service.hpp
 * 说明：machines 模块的服务接口：机型清单、添加与移除 ROM、整机机型开机。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-15
 * 备注：数据类型由 schema/machines.schema.json 生成到 generated/；请求层只经本接口使用 machines 模块。
 * ------------------------------------------------------------------------
 */

#pragma once

#include "contracts/machines/generated/machines.hpp"

namespace ember::contracts::machines {

/** 机型服务。 */
class MachineService {
 public:
  virtual ~MachineService() = default;

  /** 机型清单，顺序固定为 fc、sb97、sb2000、bbg1、bbg98、pec9588、pec586、yuxing、cec1。 */
  [[nodiscard]] virtual MachinesListResult list() const = 0;

  /**
   * 读取并检查 ROM 文件，保存为机型的 ROM，替换已有的；成功后发 machines.changed。
   * @throws shared::CoreError machines.* 的错误，见 schema/machines.schema.json。
   */
  virtual MachinesSetRomResult setRom(const MachinesSetRomParams& params) = 0;

  /**
   * 读取目录里的 .bin 文件，按内容认出各是哪个机型的 ROM，给还没有 ROM 的机型添加；已有 ROM 的机型保持不变，认不出的文件跳过；每添加一个机型发一次 machines.changed。
   * @throws shared::CoreError machines.fileNotFound、machines.fileUnreadable、machines.romAccessFailed。
   */
  virtual MachinesSetRomsResult setRoms(const MachinesSetRomsParams& params) = 0;

  /**
   * 删除机型的 ROM；没有 ROM 时没有效果；有 ROM 被删除时发 machines.changed。
   * @throws shared::CoreError machines.notFound、machines.romNotNeeded、machines.romAccessFailed。
   */
  virtual MachinesRemoveRomResult removeRom(const MachinesRemoveRomParams& params) = 0;

  /**
   * 整机机型开机：读入已保存的 ROM，装配主机，替换已有的卡带与主机；失败时原有的保持不变。
   * @throws shared::CoreError machines.* 的错误，以及 runtime.hostUnsupported。
   */
  virtual MachinesBootResult boot(const MachinesBootParams& params) = 0;
};

}  // namespace ember::contracts::machines
