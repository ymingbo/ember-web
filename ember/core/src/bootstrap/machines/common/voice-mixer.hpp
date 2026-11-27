/*
 * ------------------------------------------------------------------------
 * 名称：voice-mixer.hpp
 * 说明：语音芯片输出电平的混音器：按主时钟时间记下电平变化，取声音时混进 APU 的采样；步步高、小霸王 97、裕兴三种学习机共用。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-21
 * 备注：语音芯片与 APU 互不引用，只在主机装配里经本部件接在一起：语音芯片的电平回调调用 note，主机取声音时调用 mixInto。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <utility>
#include <vector>

#include "contracts/hardware/scheduler.hpp"

namespace ember::bootstrap {

/** 语音芯片的输出电平：按主时钟时间记下变化，取声音时混进 APU 的采样。 */
class VoiceMixer {
 public:
  /** 清空（上电与复位时）。 */
  void reset();
  /** 记一次电平变化。 */
  void note(contracts::hardware::MasterTime when, double level);
  /** 把 [上次混音的时间, now] 里的电平混进 samples（采样均匀分布在这段时间里）。 */
  void mixInto(std::vector<float>& samples, contracts::hardware::MasterTime now);

 private:
  std::vector<std::pair<contracts::hardware::MasterTime, double>> changes_;
  double level_{0};
  contracts::hardware::MasterTime mixedUntil_{0};
};

}  // namespace ember::bootstrap
