/*
 * ------------------------------------------------------------------------
 * 名称：disk2.hpp
 * 说明：5.25 英寸软驱控制器模块（金字塔 PEC-9588 与中华学习机 CEC-I 的软驱接口，
 *       寄存器与苹果 Disk II 的接口相同）：
 *       步进电机四相控制、马达与驱动器选择、读写数据寄存器、写保护检测，以及盘片在读头下按位转动。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-25
 * 备注：盘片的格式（每道扇区数、磁道数、扇区顺序）由主机型号给的“格式识别函数”按镜像认出，
 *       认不出时按没有盘片处理；
 *       模块与总线地址无关，只提供 16 个寄存器（编号 0 到 15，主机型号把它们挂到自己的地址上）：
 *         0 到 7：四个相位的关与开（编号除以 2 是相位，编号为奇数是开）；
 *         8 马达关、9 马达开；10 选驱动器 1、11 选驱动器 2；
 *         12（Q6L）读数据或开始移出、13（Q6H）装入或检测写保护、14（Q7L）读模式、15（Q7H）写模式；
 *       读：只有偶数编号的寄存器驱动数据线；读模式下返回数据寄存器（读头下一个半字节读完后，第一次
 *       读到它，之后读到 0，直到下一个半字节读完），装入态下读 14 返回写保护位（位 7）；
 *       写：写模式下写 13，或在装入态写 15，把数据装进数据寄存器；写模式下访问 12 让数据寄存器的 8 位
 *       从下一个位开始依次写到读头下，写完后读头继续写 0 位，直到装入下一个数据；
 *       盘片按位转动：每个位占的时间由主机型号给定，一圈的位数由盘片格式定，
 *       磁道的位序列由镜像按需编出（见 disk2-codec.hpp）；
 *       读出时从第一个 1 位起每 8 位组成一个半字节；写出时每个位都改写读头下的位，所以程序写得比盘片
 *       转得慢时，半字节之间会多出 0 位，不会留下旧数据；离开写模式时没有移完的位不写；
 *       写完（离开写模式）后，把这个磁道上完好的扇区解出来写回镜像；
 *       没有驱动器 1、没有选中或马达没有转时读到的全是 0；马达在转但没有盘片（或认不出镜像的格式）
 *       时，
 *       读头读到的是杂波（每个磁道一圈固定的随机位，写入被忽略），程序里的读盘超时计数才会走完、报出读盘错误；
 *       马达关指令之后马达继续转一段时间才真正停转，其间再开马达不必等盘片加速；
 *       只有一个驱动器（驱动器 1）；磁头位置按半个磁道记，奇数位置读到的是下面那个磁道；
 *       .nib 镜像的磁道直接由半字节展开，写回时从磁道里读出半字节再存回去；
 *       .woz 镜像按磁道表找磁道位序列（读头在半磁道位置时查四分之一磁道表的第“位置 × 2”项，
 *       表里没有数据的位置读到杂波），写回时把磁道的位反向填回镜像（位数不变）。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "contracts/hardware/floppy-drive.hpp"
#include "contracts/hardware/scheduler.hpp"
#include "logic/hardware/disk2/disk2-codec.hpp"

namespace ember::logic::hardware {

/** 5.25 英寸软驱控制器。 */
class Disk2 final : public contracts::hardware::EventListener {
 public:
  /** 寄存器个数。 */
  static constexpr unsigned REGISTER_COUNT = 16;
  /** 读头最多能走到的位置（半个磁道为单位）：格式化要写到磁道号 40。 */
  static constexpr unsigned MAX_HALF_TRACK = 2 * disk2::TRACK_COUNT;

  /**
   * @param scheduler 调度器：时间来自它，马达延迟停转用它的事件。
   * @param ticksPerBit 盘片上一个位占的主时钟周期数。
   * @param motorOffDelay 马达关指令之后到真正停转的主时钟周期数（约一秒）。
   * @param chooser 从镜像认出盘片格式的函数，必须一直有效。
   */
  Disk2(contracts::hardware::Scheduler& scheduler, double ticksPerBit, contracts::hardware::MasterTime motorOffDelay, disk2::FormatChooser chooser);

  Disk2(const Disk2&) = delete;
  Disk2& operator=(const Disk2&) = delete;

  /** 接上驱动器 1；空指针表示没有。驱动器必须活到本对象销毁。 */
  void attachDrive(contracts::hardware::FloppyDrive* drive);

  /**
   * 复位：相位、读写状态、马达回到初始值；上电时读头也回到 0 道。
   * @param power 是否上电。
   */
  void reset(bool power);

  /**
   * 读寄存器（有副作用：改变读写状态）。
   * @param registerNumber 寄存器编号（0 到 15）。
   * @return 寄存器驱动的数据；奇数编号的寄存器不驱动数据线，返回空。
   */
  std::optional<std::uint8_t> read(unsigned registerNumber);

  /** 写寄存器：改变读写状态；写模式下还会装入数据寄存器。 */
  void write(unsigned registerNumber, std::uint8_t value);

  // 调试与测试用
  [[nodiscard]] bool motorOn() const { return motorOn_; }
  [[nodiscard]] unsigned halfTrack() const { return halfTrack_; }
  [[nodiscard]] unsigned track() const { return halfTrack_ / 2; }
  [[nodiscard]] bool writeMode() const { return q7_; }
  [[nodiscard]] bool loadMode() const { return q6_; }
  [[nodiscard]] std::uint8_t phases() const { return magnets_; }

  // EventListener
  void onEvent(contracts::hardware::MasterTime when, std::uint32_t tag) override;

 private:
  /** 访问一个寄存器：先让盘片转到此刻，再改状态，最后按状态给出读到的值。 */
  std::optional<std::uint8_t> access(unsigned registerNumber, bool isWrite, std::uint8_t value);
  /** 相位 phase（0 到 3）变成 on，读头按磁铁的吸引移动。 */
  void setPhase(unsigned phase, bool on);
  /** 按“已选驱动器 1 且马达在转”更新驱动器的马达。 */
  void updateMotor();
  /** 镜像换了（放入、取出、替换）时丢掉已编好的磁道。 */
  void refreshMedia();
  /** 马达在转：选中驱动器 1、驱动器存在且马达开着。 */
  [[nodiscard]] bool spinning() const;
  /** 放着认得出格式的盘片（否则读头读到杂波）。 */
  [[nodiscard]] bool hasDisk() const;
  /** 磁道上一圈的位（按需编出）；磁道号超过镜像的磁道数时是一圈 0 位；没有盘片时是杂波。 */
  std::vector<std::uint8_t>& trackBits(unsigned index);
  /** 读头当前位置对应的磁道缓存下标：.woz 按半磁道位置，其余按磁道号。 */
  [[nodiscard]] unsigned ringIndex() const;
  /** .woz 镜像里读头位置（半磁道为单位）对应的磁道；没有数据时为空。 */
  [[nodiscard]] const disk2::WozTrack* wozTrackAt(unsigned halfTrack) const;
  /** 让盘片从上次处理到的位转到此刻：读模式下读出半字节，写模式下把位写到磁道上。 */
  void advance();
  /** 读数据寄存器。 */
  std::uint8_t readData();
  /** 离开写模式：把写过的磁道解码，写回镜像。 */
  void finishWrite();
  void dropCaches();

  contracts::hardware::Scheduler& scheduler_;
  double ticksPerBit_;
  contracts::hardware::MasterTime motorOffDelay_;
  disk2::FormatChooser chooser_;
  /** 当前盘片的格式；没有盘片或认不出时为空。 */
  std::optional<disk2::Format> format_;
  contracts::hardware::FloppyDrive* drive_{nullptr};
  contracts::hardware::EventId motorEvent_{contracts::hardware::NO_EVENT};

  std::uint8_t magnets_{0};
  unsigned halfTrack_{0};
  bool motorOn_{false};
  /** 选中的驱动器：0 是驱动器 1，1 是驱动器 2（没有接）。 */
  unsigned selected_{0};
  bool q6_{false};
  bool q7_{false};
  /** 数据寄存器：写模式下由程序装入。 */
  std::uint8_t latch_{0};

  /** 盘片转到的位序号（从上电起不断增加），读头下的位是它除以一圈位数的余数。 */
  std::int64_t cursor_{0};
  /** 读：正在拼的半字节、读完的半字节与它是否还没被读走。 */
  unsigned readShift_{0};
  std::uint8_t readLatch_{0};
  bool readReady_{false};
  /** 写：正在移出的数据与还剩几位。 */
  std::uint8_t shiftReg_{0};
  unsigned shiftLeft_{0};
  /** 这次写模式里是否已经改写过磁道，以及写的是哪个磁道缓存（见 ringIndex）。 */
  bool sessionDirty_{false};
  unsigned sessionTrack_{0};

  /** 已编好的磁道，按磁道缓存下标（.woz 用半磁道位置，其余用磁道号）。 */
  std::array<std::vector<std::uint8_t>, 2 * disk2::TRACK_COUNT + 1> rings_;
  /** .woz 镜像的解析结果；不是 .woz 时为空。 */
  std::optional<disk2::WozImage> woz_;
  /** 编磁道时依据的镜像：数据指针与大小，变了就重新编。 */
  const std::uint8_t* imageData_{nullptr};
  std::size_t imageSize_{0};
  bool imageInserted_{false};
  /** 编磁道时驱动器的主机一侧改动次数：变了说明镜像被改过，已编好的磁道作废。 */
  std::uint64_t hostChanges_{0};
};

}  // namespace ember::logic::hardware
