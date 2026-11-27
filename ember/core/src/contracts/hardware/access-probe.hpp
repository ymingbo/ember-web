/*
 * ------------------------------------------------------------------------
 * 名称：access-probe.hpp
 * 说明：硬件契约：总线访问探针。总线在每次读写之后通知它，调试用它实现读写断点。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * 备注：没有安装探针时总线不做任何额外的事；探针只观察，不能改变读写的结果。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <cstdint>

namespace ember::contracts::hardware {

/** 访问的方向。 */
enum class AccessKind : std::uint8_t { Read, Write };

/** 总线访问探针。 */
class AccessProbe {
 public:
  virtual ~AccessProbe() = default;

  /**
   * 一次总线访问完成。
   * @param kind 读或写。
   * @param address 总线地址。
   * @param value 读到或写入的值。
   */
  virtual void onAccess(AccessKind kind, std::uint16_t address, std::uint8_t value) = 0;
};

}  // namespace ember::contracts::hardware
