/*
 * ------------------------------------------------------------------------
 * 名称：printer-output-relay.hpp
 * 说明：打印机输出的转接：主机型号目录在运行模块里创建，而接收打印页的服务由排在它后面登记的 media 模块创建；转接对象先交给主机型号目录，之后再绑定真正的接收方。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-15
 * 备注：还没有绑定接收方时收到的页被丢弃；绑定与转发都加锁（绑定在核心启动时，转发在模拟线程上）。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <memory>
#include <mutex>
#include <utility>

#include "contracts/hardware/printer-output.hpp"

namespace ember::bootstrap {

/** 打印机输出的转接。 */
class PrinterOutputRelay final : public contracts::hardware::PrinterOutput {
 public:
  /** 绑定接收方，替换已有的。 */
  void bind(std::shared_ptr<contracts::hardware::PrinterOutput> target) {
    std::lock_guard lock(mutex_);
    target_ = std::move(target);
  }

  void outputPage(contracts::hardware::PrintedPage page) override {
    std::shared_ptr<contracts::hardware::PrinterOutput> target;
    {
      std::lock_guard lock(mutex_);
      target = target_;
    }
    if (target != nullptr) target->outputPage(std::move(page));
  }

 private:
  std::mutex mutex_;
  std::shared_ptr<contracts::hardware::PrinterOutput> target_;
};

}  // namespace ember::bootstrap
