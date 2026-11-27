/*
 * ------------------------------------------------------------------------
 * 名称：sample-ring-buffer.hpp
 * 说明：单生产者单消费者的无锁环形缓冲：模拟线程写入，音频回调线程读取，两边都不加锁、不分配内存。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * 备注：读写位置用只增不减的 64 位计数，所以“满”与“空”不会混淆；容量取不小于要求的 2 的幂。
 *       写满时丢弃多出来的最新采样（生产者不能改读位置，所以不丢最旧的），丢弃数由调用方统计。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace ember::platform {

/** 浮点采样的 SPSC 环形缓冲。 */
class SampleRingBuffer {
 public:
  /** @param capacity 至少能存放的采样数；实际容量向上取 2 的幂。 */
  explicit SampleRingBuffer(std::size_t capacity) {
    std::size_t size = 1;
    while (size < capacity) size <<= 1;
    data_.assign(size, 0.0F);
    mask_ = size - 1;
  }

  SampleRingBuffer(const SampleRingBuffer&) = delete;
  SampleRingBuffer& operator=(const SampleRingBuffer&) = delete;

  [[nodiscard]] std::size_t capacity() const { return data_.size(); }

  /** 当前缓冲里的采样数，任意线程可读（是某一时刻的近似值）。 */
  [[nodiscard]] std::size_t size() const noexcept { return static_cast<std::size_t>(head_.load(std::memory_order_acquire) - tail_.load(std::memory_order_acquire)); }

  /** 生产者：写入采样，返回实际写入的个数（缓冲不够时少于要求）。 */
  std::size_t write(std::span<const float> samples) noexcept {
    const std::uint64_t head = head_.load(std::memory_order_relaxed);
    const std::uint64_t tail = tail_.load(std::memory_order_acquire);
    const std::size_t free = data_.size() - static_cast<std::size_t>(head - tail);
    const std::size_t count = samples.size() < free ? samples.size() : free;
    for (std::size_t i = 0; i < count; ++i) data_[static_cast<std::size_t>(head + i) & mask_] = samples[i];
    head_.store(head + count, std::memory_order_release);
    return count;
  }

  /** 消费者：读出至多 out.size() 个采样，返回实际读出的个数。 */
  std::size_t read(std::span<float> out) noexcept {
    const std::uint64_t tail = tail_.load(std::memory_order_relaxed);
    const std::uint64_t head = head_.load(std::memory_order_acquire);
    const std::size_t available = static_cast<std::size_t>(head - tail);
    const std::size_t count = out.size() < available ? out.size() : available;
    for (std::size_t i = 0; i < count; ++i) out[i] = data_[static_cast<std::size_t>(tail + i) & mask_];
    tail_.store(tail + count, std::memory_order_release);
    return count;
  }

  /** 消费者：丢弃缓冲里的全部采样。 */
  void discardAll() noexcept { tail_.store(head_.load(std::memory_order_acquire), std::memory_order_release); }

 private:
  std::vector<float> data_;
  std::size_t mask_{0};
  std::atomic<std::uint64_t> head_{0};
  std::atomic<std::uint64_t> tail_{0};
};

}  // namespace ember::platform
