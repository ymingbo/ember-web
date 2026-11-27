/*
 * ------------------------------------------------------------------------
 * 名称：dma-memory.hpp
 * 说明：硬件契约：通用 DMA 访问的存储。把“源数据在哪、目的写到哪”交给主机型号，DMA 模块只管寄存器与搬运顺序。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-15
 * 备注：源有两种：BIOS ROM 与扩展 RAM；目的有两种：显存（含扩展显存）与 CPU 地址空间里的工作 RAM。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <cstdint>

namespace ember::contracts::hardware {

/** 通用 DMA 访问的存储。 */
class DmaMemory {
 public:
  virtual ~DmaMemory() = default;

  /**
   * 读一个源字节；超出存储范围的读为 0。
   * @param extendedRam 为 true 读扩展 RAM，否则读 BIOS ROM。
   * @param offset 存储内的字节偏移。
   */
  virtual std::uint8_t readSource(bool extendedRam, std::uint32_t offset) = 0;

  /**
   * 写一个字节到显存。
   * @param address 16 位显存地址：位 15 为 1 是扩展显存（经显存 bank 寄存器选 32K 的页），否则是字符 RAM。
   * @param value 字节。
   */
  virtual void writeVideo(std::uint16_t address, std::uint8_t value) = 0;

  /**
   * 写一个字节到 CPU 地址空间的工作 RAM；目标在程序窗口（$8000 以上）时由实现忽略。
   * @param address CPU 地址。
   * @param value 字节。
   */
  virtual void writeWork(std::uint16_t address, std::uint8_t value) = 0;
};

}  // namespace ember::contracts::hardware
