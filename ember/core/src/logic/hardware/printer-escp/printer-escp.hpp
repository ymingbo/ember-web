/*
 * ------------------------------------------------------------------------
 * 名称：printer-escp.hpp
 * 说明：虚拟针式打印机模块（Epson ESC/P 子集）：接在并口上，收到的字节被画成 A4 的黑白页，输出给输出方（保存成图片），不连接真实打印机。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-15
 * 备注：命令：ESC @ 复位并开始一页、ESC * 选点密度并打印位图、ESC K 60 点密度位图、ESC L 120 点密度位图、ESC 3 与 ESC A 设置行距、ESC J 进纸、ESC C 与 ESC N 读入页长与下边距（忽略）、ESC < 打印头回左端，CR、LF、FF；其余字节忽略；
 *       收到数据时没有页就开一页（不要求先有 ESC @）；
 *       页面：A4，按 200 点/英寸画成 1660x2340 像素，左边距 60、上边距 96，9 针与 24 针都按 200 点/英寸近似；
 *       输出时机：收到 FF；复位线出现上升沿；复位（断电或硬复位）；一段时间（6 秒，主时钟计）没有收到数据或控制线变化；写满一页（先输出再开新页）；控制方要求立即输出；
 *       页上没有任何点时不输出；
 *       数据在 writeData 时就被打印机收下：6576 的芯片写 $4025 时自动产生选通，选通线的电平只用来重新计时；
 *       状态线：始终联机、不忙、不缺纸、不出错，应答为高；
 *       其他：没有任何对话框；未知的 ESC 命令之后回到空闲；长度为 0 的位图命令什么也不画；写满一页先输出再开新页，不丢弃超出的点。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "contracts/hardware/parallel-port.hpp"
#include "contracts/hardware/printer-output.hpp"
#include "contracts/hardware/scheduler.hpp"

namespace ember::logic::hardware {

/** 虚拟针式打印机。 */
class PrinterEscp final : public contracts::hardware::ParallelPort, public contracts::hardware::PrinterControl, public contracts::hardware::EventListener {
 public:
  /** 页宽（像素，200 点/英寸的 A4）。 */
  static constexpr int PAGE_WIDTH = 1660;
  /** 页高（像素）。 */
  static constexpr int PAGE_HEIGHT = 2340;
  /** 左边距（像素）。 */
  static constexpr int LEFT_MARGIN = 60;
  /** 上边距（像素）。 */
  static constexpr int TOP_MARGIN = 96;
  /** 默认行距（单位：1/216 英寸，即 1/6 英寸）。 */
  static constexpr int DEFAULT_LINE_SPACE = 30;

  /**
   * @param scheduler 调度器，没有数据的超时靠它的事件。
   * @param output 输出方，必须活到本对象销毁。
   * @param timeoutTicks 没有数据多久之后输出当前页（主时钟周期数）。
   */
  PrinterEscp(contracts::hardware::Scheduler& scheduler, contracts::hardware::PrinterOutput& output, contracts::hardware::MasterTime timeoutTicks);

  ~PrinterEscp() override;

  PrinterEscp(const PrinterEscp&) = delete;
  PrinterEscp& operator=(const PrinterEscp&) = delete;

  // ParallelPort
  void writeData(std::uint8_t value) override;
  void setStrobeN(bool high) override;
  void setSelectPrinterN(bool high) override;
  void setLineFeedN(bool high) override;
  void setReset(bool high) override;
  [[nodiscard]] bool ack() const override { return true; }
  [[nodiscard]] bool paperOut() const override { return false; }
  [[nodiscard]] bool select() const override { return true; }
  [[nodiscard]] bool error() const override { return false; }
  [[nodiscard]] bool busyN() const override { return true; }
  void reset() override;

  // PrinterControl
  [[nodiscard]] bool pageHasContent() const override { return pageOpen_ && hasInk_; }
  [[nodiscard]] std::uint64_t pagesOutput() const override { return pagesOutput_; }
  void flushPage() override;

  // EventListener
  void onEvent(contracts::hardware::MasterTime when, std::uint32_t tag) override;

  /** 是否有一页正在画（收到过 ESC @ 而没有输出）。 */
  [[nodiscard]] bool pageOpen() const { return pageOpen_; }

 private:
  /** 数据流的状态。 */
  enum class State : std::uint8_t { Idle, Esc, Param, Data };

  /** 一页的画笔状态。 */
  struct Pen {
    int x{0};
    int y{0};
    /** 24 针每列 3 个字节里已经画了几个。 */
    int t{0};
    int scaleX{1};
    int scaleY{1};
    /** 最近一次位图命令里每个点纵向占的像素数，换页时用它判断有没有写满一页。 */
    int pitchY{1};
    bool mode24{false};
    bool graphicsMode{false};
    int dpiX{60};
    int dpiY{72};
    int lineSpace{DEFAULT_LINE_SPACE};
  };

  void process(std::uint8_t value);
  void processEsc(std::uint8_t command);
  void executeParams();
  void drawBits(std::uint8_t data);
  void plot(int x, int y);
  void openPage();
  /** 输出当前页（有内容时）并关闭它。 */
  void closePage();
  /** 竖直位置前进 n 个单位；超出页底时先输出再开新页。 */
  void advance(int units);
  void restartTimeout();
  void cancelTimeout();

  contracts::hardware::Scheduler& scheduler_;
  contracts::hardware::PrinterOutput& output_;
  contracts::hardware::MasterTime timeoutTicks_;
  contracts::hardware::EventId timeoutEvent_{contracts::hardware::NO_EVENT};

  State state_{State::Idle};
  std::uint8_t escCommand_{0};
  std::size_t dataLength_{0};
  std::array<std::uint8_t, 3> params_{};
  std::size_t paramCount_{0};
  std::size_t paramLength_{0};
  Pen pen_;
  bool resetLine_{false};

  bool pageOpen_{false};
  bool hasInk_{false};
  std::uint64_t pagesOutput_{0};
  /** 页面位图：每行 (PAGE_WIDTH + 7) / 8 字节，1 表示黑点；没有页时为空。 */
  std::vector<std::uint8_t> bits_;
};

}  // namespace ember::logic::hardware
