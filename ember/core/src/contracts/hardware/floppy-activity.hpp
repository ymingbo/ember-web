/*
 * ------------------------------------------------------------------------
 * 名称：floppy-activity.hpp
 * 说明：软驱活动的接收方契约：主机在软驱的马达开关变化时通知它，用来让界面显示软驱的读写灯。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-15
 * 备注：在模拟线程上调用，不能阻塞；真机的读写灯跟马达同亮同灭，所以只报告马达。
 * ------------------------------------------------------------------------
 */

#pragma once

namespace ember::contracts::hardware {

/** 软驱活动的接收方。 */
class FloppyActivity {
 public:
  virtual ~FloppyActivity() = default;

  /**
   * 一个软驱的马达开了或关了（只在状态变化时调用）。
   * @param drive 驱动器编号，从 0 开始。
   * @param on 马达是否在转。
   */
  virtual void motorChanged(unsigned drive, bool on) = 0;
};

}  // namespace ember::contracts::hardware
