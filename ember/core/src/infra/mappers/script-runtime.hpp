/*
 * ------------------------------------------------------------------------
 * 名称：script-runtime.hpp
 * 说明：mapper 脚本的沙箱：每个卡带一个独立的 QuickJS 运行时，只向脚本暴露宿主对象 ctx，并限制内存、栈与单次回调的时长；还负责查表表项的计算（tables）。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * 备注：规则见 docs/03-mapper插件规范.md 第 6 节。脚本里没有文件、网络、进程、定时器、系统时间与 Math.random，也没有 import；
 *       ctx 目前实现 header、window、nametable、irq、storage、state、log、keys、pad、device（磁带机）；timer、random、settings 属于后续阶段，脚本里是 undefined。
 *       宿主对象的实现由 ScriptBindings 提供，本类不认识卡带；quickjs.h 只在 script-runtime.cpp 里出现。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "contracts/hardware/input-event.hpp"

namespace ember::infra::mappers {

/** 执行限制，默认值见 docs/03 第 6.3 节。 */
struct ScriptLimits {
  /** 运行时内存上限（字节）。 */
  std::size_t memoryBytes{32U * 1024U * 1024U};
  /** 栈上限（字节）。 */
  std::size_t stackBytes{256U * 1024U};
  /** 一次回调（含脚本加载）的时长上限，超过即中止。 */
  std::chrono::milliseconds callTimeout{50};
};

/** 脚本给宿主对象传了不合法的参数：作为 RangeError 抛回脚本。 */
class ScriptArgumentError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

/** 脚本经 ctx.settings.get(key) 可以读到的一个设置项的值：switch 为布尔值，text、choice 为字符串。 */
struct ScriptSetting {
  std::string key;
  bool isSwitch{false};
  bool flag{false};
  std::string text;
};

/** 脚本可见的名称：宿主按下标识别窗口、存储区与状态。 */
struct ScriptLayout {
  /** 窗口标识，下标即 ScriptBindings 里的窗口号。 */
  std::vector<std::string> windows;
  /** 存储区标识，下标即存储区号。 */
  std::vector<std::string> storages;
  /** 状态名，下标即状态号。 */
  std::vector<std::string> states;
  /** 电路标识，下标即电路号。 */
  std::vector<std::string> circuits;
  /** 清单声明的设置项及当前值（目前就是清单给的默认值）。 */
  std::vector<ScriptSetting> settings;
  /** ctx.header 的内容，JSON 文本。 */
  std::string headerJson;
  /** 主机的手柄口个数，ctx.pad(port) 的有效范围是 0 到 padPorts - 1。 */
  std::size_t padPorts{0};
  /** 主机上有磁带机时 ctx.device('tape') 可用。 */
  bool hasTape{false};
};

/** 宿主对象的实现：脚本经 ctx 调用时转到这里。参数不合法时抛出 ScriptArgumentError。 */
class ScriptBindings {
 public:
  virtual ~ScriptBindings() = default;

  /** ctx.window(id).setBank(bank)：bank 不能为负。 */
  virtual void windowSetBank(std::size_t window, std::int64_t bank) = 0;
  /** ctx.window(id).setOffset(bytes)：偏移不能为负。 */
  virtual void windowSetOffset(std::size_t window, std::int64_t offset) = 0;
  /** ctx.window(id).unmap()。 */
  virtual void windowUnmap(std::size_t window) = 0;
  /** ctx.window(id).bankCount：窗口当前绑定的存储区里有几个窗口大小的 bank（至少 1）。 */
  [[nodiscard]] virtual std::size_t windowBankCount(std::size_t window) const = 0;
  /** ctx.window(id).select(storage, bank)：把窗口切到清单里该窗口列出的另一个存储区并选 bank；不在列表里报错。 */
  virtual void windowSelect(std::size_t window, std::size_t storage, std::int64_t bank) = 0;
  /** ctx.window(id).setAccess('r' 或 'rw')：窗口是否允许写入（指向 ROM 时写入仍被拒绝）。 */
  virtual void windowSetAccess(std::size_t window, bool writable) = 0;
  /** ctx.busConflict.set(enabled)：是否对寄存器写入做总线冲突。 */
  virtual void busConflictSet(bool enabled) = 0;
  /** ctx.peek(address)：按卡带自己的 CPU 侧窗口读一个字节（不经过寄存器，没有副作用）；窗口没有映射或不驱动数据线时返回空。 */
  [[nodiscard]] virtual std::optional<std::uint8_t> cpuPeek(std::uint16_t address) = 0;
  /** ctx.random(max)：0 到 max - 1 的整数，max 为 1 到 2^31；序列由卡带的种子决定，上电复位后可重现。 */
  virtual std::uint32_t randomBelow(std::uint32_t max) = 0;
  /** ctx.nametable.set(mode)：mode 是 horizontal、vertical、single0、single1、four 之一，否则报错。 */
  virtual void nametableSet(std::string_view mode) = 0;
  /** ctx.nametable.map(slot, source, bank)：名称表的第 slot 个 1K 槽（$2000、$2400、$2800、$2C00）映射到 'ciram0'、'ciram1'（主板显存的两页）或存储区 source 的第 bank 个 1K 页；清单的 nametable 不是 custom 时抛出 ScriptArgumentError。 */
  virtual void nametableMap(std::size_t slot, std::string_view source, std::int64_t bank) = 0;
  /** ctx.irq.assert()。 */
  virtual void irqAssert() = 0;
  /** ctx.irq.release()。 */
  virtual void irqRelease() = 0;
  /** ctx.storage(id).size。 */
  [[nodiscard]] virtual std::size_t storageSize(std::size_t storage) const = 0;
  /** ctx.storage(id).read(offset)：偏移越界报错。 */
  virtual std::uint8_t storageRead(std::size_t storage, std::int64_t offset) = 0;
  /** ctx.storage(id).write(offset, value)：偏移越界报错；ROM 拒绝写入，返回 false。 */
  virtual bool storageWrite(std::size_t storage, std::int64_t offset, std::uint8_t value) = 0;
  /** ctx.state 里一项的读取。 */
  [[nodiscard]] virtual std::uint32_t stateGet(std::size_t state) const = 0;
  /** ctx.state 里一项的写入：宿主按位宽截断。 */
  virtual void stateSet(std::size_t state, std::uint32_t value) = 0;
  /** ctx.log.warn(message)。 */
  virtual void log(std::string_view message) = 0;
  /** ctx.keys.isDown(code)：这个物理键现在是否按着。 */
  [[nodiscard]] virtual bool keyDown(std::string_view code) const = 0;
  /** ctx.pad(port).set(button, down)：button 是 a、b、select、start、up、down、left、right 之一，否则报错。 */
  virtual void padSet(std::size_t port, std::string_view button, bool down) = 0;
  /** ctx.pad(port).release()：松开这个口的全部按键。 */
  virtual void padRelease(std::size_t port) = 0;
  /** ctx.device('tape').level()：磁带送给卡带的 1 位信号。 */
  [[nodiscard]] virtual bool tapeLevel() = 0;
  /** ctx.device('tape').setComparator(low, high, invert)：设置数据线的比较器。 */
  virtual void tapeSetComparator(std::int16_t low, std::int16_t high, bool invert) = 0;
  /** ctx.cycle()：自上电起的 CPU 周期数；主机没有调度器时抛出 ScriptArgumentError。 */
  [[nodiscard]] virtual std::uint64_t cycle() = 0;
  /** ctx.timer.after(name, cycles)：cycles 个 CPU 周期之后调用 onTimer(ctx, name)；同名的计时已在等待时先取消；没有调度器时抛出 ScriptArgumentError。 */
  virtual void timerAfter(std::string_view name, std::int64_t cycles) = 0;
  /** ctx.timer.cancel(name)：取消还没有到期的同名计时，没有时什么也不做。 */
  virtual void timerCancel(std::string_view name) = 0;
  /** ctx.circuit(id).write(address, value)：把一次写入交给电路，电路按清单的 wire 自动设置窗口（不再回调 onCircuit）。 */
  virtual void circuitWrite(std::size_t circuit, std::uint16_t address, std::uint8_t value) = 0;
  /** ctx.circuit(id).get(name)：电路公开的一项内部状态（如 MMC3 的 r0-r7、selector）；没有这一项时抛出 ScriptArgumentError。 */
  [[nodiscard]] virtual std::uint32_t circuitGet(std::size_t circuit, std::string_view name) = 0;
  /** ctx.circuit(id).bank(space, address)：电路给出的页号，space 是 'program' 或 'character'。 */
  [[nodiscard]] virtual std::uint32_t circuitBank(std::size_t circuit, bool character, std::uint16_t address) = 0;
};

/** 一个插件的脚本运行时。非线程安全：同一时刻只能由一个线程使用，换线程使用没有问题。 */
class ScriptRuntime {
 public:
  /**
   * 创建运行时并加载脚本（ES 模块，默认导出一个对象，对象上可以有 reset、read、write、onKey、onMouse 几个函数）。
   * @param pluginId 插件标识，出现在错误说明里。
   * @param source main.js 的内容。
   * @param layout ctx 里可见的窗口、存储区与状态。
   * @param bindings 宿主对象的实现，必须活到本对象销毁。
   * @param limits 执行限制。
   * @throws shared::CoreError mappers.invalidPackage：脚本无法加载或导出不合格；mappers.limitExceeded：加载时超限。
   */
  ScriptRuntime(std::string pluginId, std::string_view source, ScriptLayout layout, ScriptBindings& bindings, ScriptLimits limits = {});
  ~ScriptRuntime();

  ScriptRuntime(const ScriptRuntime&) = delete;
  ScriptRuntime& operator=(const ScriptRuntime&) = delete;

  /** 脚本是否导出了 reset。 */
  [[nodiscard]] bool hasReset() const noexcept;
  /** 脚本是否导出了 read。 */
  [[nodiscard]] bool hasRead() const noexcept;
  /** 脚本是否导出了 write。 */
  [[nodiscard]] bool hasWrite() const noexcept;
  /** 脚本是否导出了 onKey。 */
  [[nodiscard]] bool hasKey() const noexcept;
  /** 脚本是否导出了 onMouse。 */
  [[nodiscard]] bool hasMouse() const noexcept;
  /** 脚本是否导出了 onTapeEdge。 */
  [[nodiscard]] bool hasTapeEdge() const noexcept;
  /** 脚本是否导出了 onTimer。 */
  [[nodiscard]] bool hasTimer() const noexcept;
  /** 脚本是否导出了 onCircuit。 */
  [[nodiscard]] bool hasCircuit() const noexcept;
  /** 脚本是否导出了 onA12。 */
  [[nodiscard]] bool hasA12() const noexcept;
  /** 脚本是否在 tables 对象上导出了这个标识的函数。 */
  [[nodiscard]] bool hasTable(std::string_view id) const;

  /** 调用 reset(ctx, 'power' 或 'soft')；脚本没有 reset 时什么也不做。 */
  void reset(bool power);

  /**
   * 调用 read(ctx, bus, addr)。
   * @return 脚本返回的 0-255 的值；返回 undefined 表示不驱动总线，对应空。
   * @throws shared::CoreError mappers.scriptError：脚本异常或返回值不合法；mappers.limitExceeded：超限。
   */
  [[nodiscard]] std::optional<std::uint8_t> read(bool ppu, std::uint16_t address);

  /** 调用 write(ctx, bus, addr, value)。错误同 read。 */
  void write(bool ppu, std::uint16_t address, std::uint8_t value);

  /** 调用 onKey(ctx, {code, down})；脚本没有 onKey 时什么也不做。错误同 read。 */
  void key(const contracts::hardware::KeyInput& input);

  /** 调用 onMouse(ctx, {kind, x, y, dx, dy, buttons, wheel})；脚本没有 onMouse 时什么也不做。错误同 read。 */
  void mouse(const contracts::hardware::MouseInput& input);

  /** 调用 onTapeEdge(ctx, level, samples)：磁带数据线翻转时，level 是翻转后的电平（0 或 1），samples 是距上一次翻转的采样数（96 kHz）；脚本没有 onTapeEdge 时什么也不做。错误同 read。 */
  void tapeEdge(bool level, std::uint32_t samples);

  /** 调用 onTimer(ctx, name)：ctx.timer.after 登记的计时到期；脚本没有 onTimer 时什么也不做。错误同 read。 */
  void timer(std::string_view name);

  /** 调用 onCircuit(ctx, id)：清单声明的写寄存器把写入交给电路之后，脚本按电路的新状态重新设置窗口；脚本没有 onCircuit 时什么也不做。错误同 read。 */
  void circuit(std::string_view id);

  /** 调用 onA12(ctx)：PPU 地址线 A12 出现一次上升沿（先低过若干次显存访问，滤掉取图块时的短暂低电平）；脚本没有 onA12 时什么也不做。错误同 read。 */
  void a12();

  /**
   * 调用 tables.<id>(ctx, ...values)：计算查表的一个表项，values 是索引各来源的值，函数必须是纯函数（同样的输入得到同样的结果，不改变卡带状态）。
   * @return 脚本返回的 0 到 2^32-1 的整数。
   * @throws shared::CoreError mappers.scriptError：没有这个函数、脚本异常或返回值不合法；mappers.limitExceeded：超限。
   */
  [[nodiscard]] std::uint32_t table(std::string_view id, std::span<const std::uint32_t> values);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace ember::infra::mappers
