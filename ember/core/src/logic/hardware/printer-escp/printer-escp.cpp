/*
 * ------------------------------------------------------------------------
 * 名称：printer-escp.cpp
 * 说明：虚拟针式打印机模块的实现。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-15
 * ------------------------------------------------------------------------
 */

#include "logic/hardware/printer-escp/printer-escp.hpp"

#include <algorithm>
#include <utility>

namespace ember::logic::hardware {

namespace hw = contracts::hardware;

namespace {

constexpr std::uint8_t CHAR_LF = 0x0A;
constexpr std::uint8_t CHAR_FF = 0x0C;
constexpr std::uint8_t CHAR_CR = 0x0D;
constexpr std::uint8_t CHAR_ESC = 0x1B;

constexpr std::uint8_t ESC_HEAD_LEFT = 0x3C;      // <
constexpr std::uint8_t ESC_RESET = 0x40;          // @
constexpr std::uint8_t ESC_LINE_SPACING = 0x33;   // 3
constexpr std::uint8_t ESC_LINE_SPACING_72 = 0x41;  // A
constexpr std::uint8_t ESC_GRAPHICS_60 = 0x4B;    // K
constexpr std::uint8_t ESC_ADVANCE = 0x4A;        // J
constexpr std::uint8_t ESC_BOTTOM_MARGIN = 0x4E;  // N
constexpr std::uint8_t ESC_PAGE_LENGTH = 0x43;    // C
constexpr std::uint8_t ESC_GRAPHICS_120 = 0x4C;   // L
constexpr std::uint8_t ESC_GRAPHICS = 0x2A;       // *

/** 点密度表：m 是 ESC * 的第一个参数。 */
struct DotDensity {
  int mode;
  int dpiX;
  int dpiY;
  int dots;
};

constexpr DotDensity DOT_DENSITIES[] = {
    {0, 60, 72, 8}, {1, 120, 72, 8}, {2, 120, 72, 8}, {3, 240, 72, 8}, {4, 80, 72, 8},    {5, 72, 72, 8},   {6, 90, 72, 8},
    {7, 144, 72, 8}, {32, 60, 180, 24}, {33, 120, 180, 24}, {38, 90, 180, 24}, {39, 180, 180, 24}, {40, 360, 180, 24},
};

/** 一行位图的字节数。 */
constexpr int ROW_BYTES = (PrinterEscp::PAGE_WIDTH + 7) / 8;

}  // namespace

PrinterEscp::PrinterEscp(hw::Scheduler& scheduler, hw::PrinterOutput& output, hw::MasterTime timeoutTicks) : scheduler_(scheduler), output_(output), timeoutTicks_(timeoutTicks) {}

PrinterEscp::~PrinterEscp() { cancelTimeout(); }

// ---------------------------------------------------------------- 并口

void PrinterEscp::writeData(std::uint8_t value) {
  // 真实的针式打印机不需要 ESC @ 也能打印：收到数据时没有页就开一页。
  if (!pageOpen_) openPage();
  restartTimeout();
  process(value);
}

void PrinterEscp::setStrobeN(bool /*high*/) { restartTimeout(); }

void PrinterEscp::setSelectPrinterN(bool /*high*/) { restartTimeout(); }

void PrinterEscp::setLineFeedN(bool /*high*/) { restartTimeout(); }

void PrinterEscp::setReset(bool high) {
  if (high == resetLine_) return;
  resetLine_ = high;
  restartTimeout();
  if (!high) return;
  // 复位线出现上升沿：输出当前页，回到空闲。
  closePage();
  state_ = State::Idle;
  dataLength_ = 0;
}

void PrinterEscp::reset() {
  cancelTimeout();
  closePage();
  state_ = State::Idle;
  dataLength_ = 0;
  resetLine_ = false;
}

void PrinterEscp::flushPage() {
  if (pageHasContent()) closePage();
}

void PrinterEscp::onEvent(hw::MasterTime /*when*/, std::uint32_t /*tag*/) {
  timeoutEvent_ = hw::NO_EVENT;
  closePage();
}

void PrinterEscp::restartTimeout() {
  cancelTimeout();
  if (pageOpen_) timeoutEvent_ = scheduler_.scheduleAt(scheduler_.now() + timeoutTicks_, *this, 0);
}

void PrinterEscp::cancelTimeout() {
  if (timeoutEvent_ != hw::NO_EVENT) scheduler_.cancel(timeoutEvent_);
  timeoutEvent_ = hw::NO_EVENT;
}

// ---------------------------------------------------------------- 数据流

void PrinterEscp::process(std::uint8_t value) {
  switch (state_) {
    case State::Data:
      drawBits(value);
      if (--dataLength_ == 0) {
        state_ = State::Idle;
        if (pen_.graphicsMode) {
          pen_.graphicsMode = false;
          pen_.scaleX = 1;
          pen_.scaleY = 1;
        } else {
          pen_.x = 0;
        }
      }
      break;

    case State::Idle:
      switch (value) {
        case CHAR_LF:
          // 行距的单位：24 针是 1/180 英寸，9 针是 1/216 英寸。
          advance(pen_.lineSpace * pen_.dpiY / (pen_.mode24 ? 180 : 216));
          break;
        case CHAR_FF:
          closePage();
          break;
        case CHAR_CR:
          pen_.x = 0;
          break;
        case CHAR_ESC:
          state_ = State::Esc;
          break;
        default:
          break;
      }
      break;

    case State::Esc:
      processEsc(value);
      break;

    case State::Param:
      params_[paramCount_++] = value;
      if (paramCount_ >= paramLength_) executeParams();
      break;
  }
}

void PrinterEscp::processEsc(std::uint8_t command) {
  escCommand_ = command;
  paramCount_ = 0;
  switch (command) {
    case ESC_HEAD_LEFT:
      pen_.x = 0;
      state_ = State::Idle;
      break;
    case ESC_RESET:
      if (!pageOpen_) openPage();
      pen_.x = 0;
      state_ = State::Idle;
      break;
    case ESC_LINE_SPACING:
    case ESC_LINE_SPACING_72:
    case ESC_ADVANCE:
    case ESC_BOTTOM_MARGIN:
      paramLength_ = 1;
      state_ = State::Param;
      break;
    case ESC_PAGE_LENGTH:
    case ESC_GRAPHICS_120:
    case ESC_GRAPHICS_60:
      paramLength_ = 2;
      state_ = State::Param;
      break;
    case ESC_GRAPHICS:
      paramLength_ = 3;
      state_ = State::Param;
      break;
    default:
      // 不认识的命令：忽略，回到空闲。
      state_ = State::Idle;
      break;
  }
}

void PrinterEscp::executeParams() {
  state_ = State::Idle;
  switch (escCommand_) {
    case ESC_GRAPHICS: {
      const DotDensity* found = nullptr;
      for (const DotDensity& density : DOT_DENSITIES) {
        if (density.mode == params_[0]) {
          found = &density;
          break;
        }
      }
      if (found == nullptr) {
        // 不认识的密度：按 24 针 180 点/英寸处理。
        pen_.dpiX = 180;
        pen_.dpiY = 180;
        pen_.mode24 = true;
      } else {
        pen_.dpiX = found->dpiX;
        pen_.dpiY = found->dpiY;
        pen_.mode24 = found->dots == 24;
      }
      std::size_t length = static_cast<std::size_t>(params_[2]) * 256 + params_[1];
      if (pen_.mode24) length *= 3;
      pen_.t = 0;
      pen_.pitchY = 1;
      if (length > 0) {
        dataLength_ = length;
        state_ = State::Data;
      }
      break;
    }
    case ESC_LINE_SPACING:
      pen_.lineSpace = params_[0];
      break;
    case ESC_LINE_SPACING_72:
      // n/72 英寸（24 针机是 n/60 英寸），折成与 ESC 3 相同的单位。
      pen_.lineSpace = params_[0] * 3;
      break;
    case ESC_GRAPHICS_60: {
      // 60 点/英寸的 8 针位图：每点横纵各占 3 个像素，一列一个字节。
      pen_.dpiX = 60;
      pen_.dpiY = 72;
      pen_.mode24 = false;
      const std::size_t length = static_cast<std::size_t>(params_[1]) * 256 + params_[0];
      pen_.t = 0;
      pen_.pitchY = 3;
      if (length > 0) {
        pen_.scaleX = 3;
        pen_.scaleY = 3;
        pen_.graphicsMode = true;
        dataLength_ = length;
        state_ = State::Data;
      }
      break;
    }
    case ESC_ADVANCE:
      advance(params_[0] * pen_.dpiY / (pen_.mode24 ? 180 : 216));
      break;
    case ESC_GRAPHICS_120: {
      // 120 点/英寸的位图：24 针机是 120x60，9 针机是 120x72，纵向每点占两行。
      pen_.dpiX = 120;
      pen_.dpiY = pen_.mode24 ? 60 : 72;
      const std::size_t length = static_cast<std::size_t>(params_[1]) * 256 + params_[0];
      pen_.t = 0;
      pen_.pitchY = 2;
      if (length > 0) {
        pen_.scaleY = 2;
        pen_.graphicsMode = true;
        dataLength_ = length;
        state_ = State::Data;
      }
      break;
    }
    default:
      // ESC C 页长与 ESC N 下边距：读入后忽略。
      break;
  }
}

// ---------------------------------------------------------------- 画页

void PrinterEscp::openPage() {
  pageOpen_ = true;
  hasInk_ = false;
  bits_.assign(static_cast<std::size_t>(ROW_BYTES) * PAGE_HEIGHT, 0);
  pen_ = Pen{};
  state_ = State::Idle;
  dataLength_ = 0;
}

void PrinterEscp::closePage() {
  cancelTimeout();
  if (!pageOpen_) return;
  const bool printed = hasInk_;
  hw::PrintedPage page;
  if (printed) {
    page.width = PAGE_WIDTH;
    page.height = PAGE_HEIGHT;
    page.rows = std::move(bits_);
  }
  pageOpen_ = false;
  hasInk_ = false;
  bits_.clear();
  bits_.shrink_to_fit();
  if (printed) {
    ++pagesOutput_;
    output_.outputPage(std::move(page));
  }
}

void PrinterEscp::advance(int units) {
  pen_.y += units;
  if (!pageOpen_ || (TOP_MARGIN + pen_.y) * pen_.pitchY < PAGE_HEIGHT) return;
  // 写满一页：先输出再开新页，竖直位置回到页首，打印头的横向位置与行距不变。
  const Pen keep = pen_;
  closePage();
  openPage();
  pen_ = keep;
  pen_.y = 0;
}

void PrinterEscp::drawBits(std::uint8_t data) {
  if (pageOpen_) {
    for (int bit = 0; bit < 8; ++bit) {
      if ((data & (0x80 >> bit)) != 0) plot(pen_.x, pen_.y + pen_.t * 8 + bit);
    }
  }
  ++pen_.t;
  if (pen_.graphicsMode || !pen_.mode24) {
    pen_.t = 0;
    ++pen_.x;
  } else if (pen_.t == 3) {
    pen_.t = 0;
    ++pen_.x;
  }
}

void PrinterEscp::plot(int x, int y) {
  const int left = (x + LEFT_MARGIN) * pen_.scaleX;
  const int top = (y + TOP_MARGIN) * pen_.scaleY;
  const int right = std::min(left + pen_.scaleX, PAGE_WIDTH);
  const int bottom = std::min(top + pen_.scaleY, PAGE_HEIGHT);
  for (int py = std::max(top, 0); py < bottom; ++py) {
    for (int px = std::max(left, 0); px < right; ++px) {
      bits_[static_cast<std::size_t>(py) * ROW_BYTES + static_cast<std::size_t>(px / 8)] |= static_cast<std::uint8_t>(0x80 >> (px % 8));
      hasInk_ = true;
    }
  }
}

}  // namespace ember::logic::hardware
