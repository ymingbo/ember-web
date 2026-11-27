/*
 * ------------------------------------------------------------------------
 * 名称：hardware-error.hpp
 * 说明：硬件模块的错误码：硬件契约与实现共用，经 shared::CoreError 抛出。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * 备注：错误码以模块名 hardware 开头；这些错误都是装配或调用方的错误，不是运行中的正常情况。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <string_view>

namespace ember::contracts::hardware {

/** 总线地址范围不合法：起点大于终点，或超出总线的地址宽度。 */
inline constexpr std::string_view ERROR_BUS_RANGE_INVALID = "hardware.busRangeInvalid";
/** 总线地址范围与已挂载的范围重叠。 */
inline constexpr std::string_view ERROR_BUS_RANGE_CONFLICT = "hardware.busRangeConflict";
/** 时钟被要求向后推进，或在事件回调里被推进。 */
inline constexpr std::string_view ERROR_CLOCK_INVALID_ADVANCE = "hardware.clockInvalidAdvance";
/** 信号线的来源数量超过上限。 */
inline constexpr std::string_view ERROR_LINE_SOURCE_LIMIT = "hardware.lineSourceLimit";
/** CPU 的 RDY 线长时间有效，暂停方没有释放。 */
inline constexpr std::string_view ERROR_CPU_STALLED = "hardware.cpuStalled";
/** 构造参数不合法（如分频比不大于 0）。 */
inline constexpr std::string_view ERROR_INVALID_ARGUMENT = "hardware.invalidArgument";
/** 存储区的偏移或读取范围超出容量。 */
inline constexpr std::string_view ERROR_STORAGE_OUT_OF_RANGE = "hardware.storageOutOfRange";
/** 端口编号超出主机上的端口数。 */
inline constexpr std::string_view ERROR_PORT_NOT_FOUND = "hardware.portNotFound";
/** 调试访问的内存空间不存在。 */
inline constexpr std::string_view ERROR_UNKNOWN_SPACE = "hardware.unknownSpace";
/** 调试访问的范围超出内存空间的大小。 */
inline constexpr std::string_view ERROR_SPACE_OUT_OF_RANGE = "hardware.spaceOutOfRange";
/** 调试写入的目标是只读的（ROM），或这个空间不能写。 */
inline constexpr std::string_view ERROR_SPACE_READ_ONLY = "hardware.spaceReadOnly";
/** 调试设置的状态项不存在，或取值超出范围。 */
inline constexpr std::string_view ERROR_INVALID_STATE = "hardware.invalidState";
/** 断点不合格：地址范围的起点大于终点。 */
inline constexpr std::string_view ERROR_INVALID_BREAKPOINT = "hardware.invalidBreakpoint";
/** 断点数量超过上限。 */
inline constexpr std::string_view ERROR_BREAKPOINT_LIMIT = "hardware.breakpointLimit";
/** 断点的条件表达式不合格：写法错误或超过长度上限。 */
inline constexpr std::string_view ERROR_INVALID_CONDITION = "hardware.invalidCondition";
/** 同时进行的记录超过上限。 */
inline constexpr std::string_view ERROR_RECORDER_LIMIT = "hardware.recorderLimit";
/** 没有这个编号的记录。 */
inline constexpr std::string_view ERROR_NO_RECORDER = "hardware.noRecorder";
/** 代码与数据标记已经在记录。 */
inline constexpr std::string_view ERROR_CDL_ACTIVE = "hardware.cdlActive";

}  // namespace ember::contracts::hardware
