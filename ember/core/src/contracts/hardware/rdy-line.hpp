/*
 * ------------------------------------------------------------------------
 * 名称：rdy-line.hpp
 * 说明：硬件契约：RDY 线，CPU 的暂停线，DMA 用它暂停 CPU。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * 备注：与真机一致：CPU 只在读周期暂停，写周期照常完成；暂停期间 CPU 不访问总线，总线由暂停方使用，
 *       CPU 的时钟照常走，暂停方靠调度器事件推进自己的传输并在完成时释放。
 * ------------------------------------------------------------------------
 */

#pragma once

#include "contracts/hardware/signal-line.hpp"

namespace ember::contracts::hardware {

/** CPU 暂停线：有效表示要求 CPU 暂停。 */
class RdyLine : public SignalLine {};

}  // namespace ember::contracts::hardware
