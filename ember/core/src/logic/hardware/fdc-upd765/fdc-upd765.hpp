/*
 * ------------------------------------------------------------------------
 * 名称：fdc-upd765.hpp
 * 说明：软驱控制器模块（uPD765 兼容）：命令阶段、执行阶段、结果阶段，读写扇区、读扇区编号、格式化、寻道、回零、取中断状态、取驱动器状态、设定与配置，以及轮询、DMA 请求与中断。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-15
 * 备注：模块本身与总线地址无关，只提供 8 个端口的读写（端口号 0-7，对应标准 PC 软驱控制器的寄存器），
 *       主机型号把它挂到自己的地址译码表上；数据经“DMA 数据口”逐字节交给 CPU（请求位由 CPU 查询），不占用真正的 DMA 通道；
 *       状态机由调度器事件推进：每 stepTicks 个主时钟周期执行一步，空闲（命令阶段等待命令字节、轮询已完成）时不登记事件，任何端口访问都会重新启动；
 *       中断：中断标志与 DOR 位 3（允许中断）同时成立时，在出现上升沿的那一刻向中断汇集器请求一次。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <initializer_list>
#include <optional>
#include <vector>

#include "contracts/hardware/floppy-drive.hpp"
#include "contracts/hardware/interrupt-sink.hpp"
#include "contracts/hardware/scheduler.hpp"

namespace ember::logic::hardware {

/** 软驱控制器。 */
class FdcUpd765 final : public contracts::hardware::EventListener {
 public:
  /** 控制器能接的驱动器数。 */
  static constexpr unsigned DRIVE_COUNT = 4;

  /**
   * @param scheduler 调度器，状态机的推进靠它的事件。
   * @param sink 中断汇集器。
   * @param interruptSource 请求中断时用的来源位。
   * @param stepTicks 状态机每一步占的主时钟周期数。
   * @param pollSteps 轮询一个驱动器占的步数（芯片手册：约 220 微秒）。
   */
  FdcUpd765(contracts::hardware::Scheduler& scheduler, contracts::hardware::InterruptSink& sink, std::uint8_t interruptSource, contracts::hardware::MasterTime stepTicks, unsigned pollSteps);

  FdcUpd765(const FdcUpd765&) = delete;
  FdcUpd765& operator=(const FdcUpd765&) = delete;

  /** 接上一个驱动器；空指针表示没有这个驱动器。驱动器必须活到本对象销毁。 */
  void attachDrive(unsigned index, contracts::hardware::FloppyDrive* drive);

  /**
   * 复位。
   * @param power 为 true 时寄存器（DOR、配置、数据率）也回到上电值，驱动器的马达停转。
   */
  void reset(bool power);

  /** 读端口（0-7）：0 状态寄存器 A，2 DOR，3 中断位（位 6），4 主状态，5 数据，7 盘片更换位（读后清除）；读数据与盘片更换位有副作用。 */
  std::uint8_t readRegister(unsigned port);

  /** 无副作用地读端口：只有 0、2、4、5 有内容，其余为 $FF。 */
  [[nodiscard]] std::uint8_t peekRegister(unsigned port) const;

  /** 写端口（0-7）：2 DOR，3 清中断标志，5 数据，7 数据率。 */
  void writeRegister(unsigned port, std::uint8_t value);

  // ------------------------------------------------ DMA 数据口

  /** 数据请求位：控制器有一个字节要交给 CPU，或等着 CPU 给下一个字节。 */
  [[nodiscard]] bool dmaRequest() const { return dmaRequest_; }
  /** CPU 读 DMA 数据口：取走数据、清请求位。 */
  std::uint8_t receiveFromDma();
  /** CPU 写 DMA 数据口：交出数据、清请求位。 */
  void sendToDma(std::uint8_t value);
  /** 终止计数：当前扇区之后结束命令。 */
  void setTerminalCount();
  /** 控制器复位引脚；出现上升沿时复位控制器。 */
  void setResetPin(bool high);

  /** 中断标志（状态寄存器 A 位 7）。 */
  [[nodiscard]] bool irqRaised() const { return (sra_ & SRA_IRQ) != 0; }

  // 测试与调试用
  [[nodiscard]] std::uint8_t dor() const { return dor_; }
  [[nodiscard]] std::uint8_t mainStatus() const { return msr_; }
  [[nodiscard]] bool stepScheduled() const { return eventId_ != contracts::hardware::NO_EVENT; }

  // EventListener
  void onEvent(contracts::hardware::MasterTime when, std::uint32_t tag) override;

 private:
  using Handler = void (FdcUpd765::*)();

  /** 一条命令：包括命令字节在内的字节数与处理函数。 */
  struct CommandInfo {
    std::size_t length;
    Handler handler;
  };

  static constexpr std::uint8_t SRA_IRQ = 0x80;

  // 阶段
  void commandPhase();
  void resultPhase();
  void endCommand(bool raiseIrq, std::initializer_list<std::uint8_t> results);
  void nextSector();
  void pollDrives();

  // 命令
  void cmReadId();
  void cmReadData();
  void cmWriteData();
  void cmFormatTrack();
  void cmSeek();
  void cmRecalibrate();
  void cmSenseInterruptStatus();
  void cmSpecify();
  void cmSenseDriveStatus();
  void cmConfigure();
  void cmInvalid();

  /** 命令的扇区大小码（结果里原样带回）。 */
  [[nodiscard]] std::uint8_t sectorSizeCode() const;
  /** 取当前选中的驱动器；没有接驱动器时是一个不存在的驱动器。 */
  [[nodiscard]] contracts::hardware::FloppyDrive& drive();
  [[nodiscard]] contracts::hardware::FloppyDrive& driveAt(unsigned index) const;
  /** 重新开始状态机（命令之间的复位：命令阶段、清队列）。 */
  void resetController();
  /** 状态机一步。 */
  void step();
  /** 没有事情可做：不需要再登记事件。 */
  [[nodiscard]] bool idle() const;
  /** 有驱动器被注入了“控制器无响应”的故障。 */
  [[nodiscard]] bool unresponsive() const;
  /** 需要时登记下一步。 */
  void arm();
  /** 检查中断：出现上升沿时请求中断。 */
  void checkInterrupt();

  contracts::hardware::Scheduler& scheduler_;
  contracts::hardware::InterruptSink& sink_;
  std::uint8_t interruptSource_;
  contracts::hardware::MasterTime stepTicks_;
  unsigned pollSteps_;
  contracts::hardware::EventId eventId_{contracts::hardware::NO_EVENT};

  std::array<contracts::hardware::FloppyDrive*, DRIVE_COUNT> drives_{};

  std::uint8_t dor_{0};
  std::uint8_t msr_{0};
  std::uint8_t st0_{0};
  std::uint8_t st1_{0};
  std::uint8_t st2_{0};
  std::uint8_t sra_{0};
  std::uint8_t config_{0};
  std::uint8_t latch_{0};
  std::uint8_t cylinder_{0};
  std::uint8_t head_{0};
  std::uint8_t sectorNumber_{0};
  /** 当前扇区在镜像里的位置；没有时表示还没有开始传输。 */
  std::optional<std::size_t> dataPosition_;
  bool resetPin_{false};
  bool useDma_{false};
  bool dmaRequest_{false};
  bool dmaTerminate_{false};
  bool irqLevel_{false};
  unsigned dataRate_{500};
  std::size_t sectorSize_{0};
  std::size_t bytesLeft_{0};
  Handler phase_{nullptr};
  std::vector<std::uint8_t> command_;
  std::deque<std::uint8_t> resultQueue_;

  unsigned pollTimer_{0};
  unsigned pollDrive_{0};
  unsigned pollDrivesLeft_{0};
  std::array<bool, DRIVE_COUNT> previouslyReady_{};

  static const std::array<CommandInfo, 0x20> COMMANDS;
};

}  // namespace ember::logic::hardware
