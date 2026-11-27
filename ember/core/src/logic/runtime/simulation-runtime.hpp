/*
 * ------------------------------------------------------------------------
 * 名称：simulation-runtime.hpp
 * 说明：运行服务的实现：一条模拟线程拥有全部硬件状态（主机与卡带），其他线程的请求投递到命令队列，由模拟线程在帧边界执行。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * 备注：free 模式按真实帧率连续运行（按帧节拍休眠，休眠中收到命令会提前醒来），paused 停止；stepping 与速度调节以后再做。
 *       每帧完成后把帧缓冲交给帧输出，发 runtime.frame（间隔为 0 时不发）；每帧与每条命令之后检查卡带脚本是否出错，
 *       出错就暂停并发 runtime.error。停止时拒绝新命令、结束线程，排队中的命令以 core.unavailable 完成。
 *       声音：每帧完成后把主机生成的采样写进声音输出（环形缓冲）；暂停、断电、出错时声音输出淡出到静音，继续运行时清空过时的采样并淡入，
 *       上电与复位时清空缓冲；没有声音输出（空指针）或采样率与主机不同时不输出声音。
 *       授权运行（startRun）：不限速地连续跑指定的帧数后暂停，运行中不输出声音，每帧之间照常处理命令队列（输入、暂停等）；
 *       完成或被打断时把结果交给等待的调用方并发 runtime.runFinished；停止时正在进行的授权运行以 interrupted 结束。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

#include "contracts/hardware/machine.hpp"
#include "contracts/mappers/mapper-cartridge.hpp"
#include "contracts/runtime/machine-factory.hpp"
#include "contracts/runtime/runtime-service.hpp"
#include "platform/audio/audio-output.hpp"
#include "platform/events/event-sink.hpp"
#include "platform/logging/error-log.hpp"
#include "platform/streams/frame-output.hpp"

namespace ember::logic::runtime {

/** NTSC 经典 FC 的帧率（每秒帧数）。 */
inline constexpr double NTSC_FRAMES_PER_SECOND = 60.0988;
/** framesPerSecond 取这个值时，按装入的主机自己的帧率限速。 */
inline constexpr double MACHINE_FRAME_RATE = -1.0;

/** 运行服务的依赖与参数。 */
struct SimulationRuntimeOptions {
  std::shared_ptr<contracts::runtime::MachineFactory> machines;
  std::shared_ptr<contracts::mappers::MapperFactory> mappers;
  std::shared_ptr<platform::FrameOutput> frames;
  /** 声音输出；为空时不输出声音。 */
  std::shared_ptr<platform::AudioOutput> audio;
  std::shared_ptr<platform::EventSink> events;
  std::shared_ptr<platform::ErrorLog> errors;
  /** 连续运行时的帧率；MACHINE_FRAME_RATE 表示按主机自己的帧率，0 表示不限速、一直全速运行（测试用）。 */
  double framesPerSecond{MACHINE_FRAME_RATE};
  /** 模拟线程启动时在该线程里调用，返回值持有到线程结束（用来提高线程优先级等）；为空时什么也不做。 */
  std::function<std::shared_ptr<void>()> threadScope;
};

/** 模拟线程与命令队列。 */
class SimulationRuntime final : public contracts::runtime::RuntimeService {
 public:
  /** 创建并启动模拟线程。 */
  explicit SimulationRuntime(SimulationRuntimeOptions options);
  ~SimulationRuntime() override;

  SimulationRuntime(const SimulationRuntime&) = delete;
  SimulationRuntime& operator=(const SimulationRuntime&) = delete;

  /** 停止：拒绝新命令，结束模拟线程并销毁主机与卡带；可重复调用。 */
  void stop();

  [[nodiscard]] contracts::runtime::RuntimeStatusResult status() const override;
  [[nodiscard]] std::optional<contracts::runtime::CartridgeSummary> cartridge() const override;
  contracts::runtime::RuntimeState powerOn(bool resume) override;
  contracts::runtime::RuntimeState powerOff() override;
  contracts::runtime::RuntimeState reset(contracts::runtime::ResetKind kind) override;
  contracts::runtime::RuntimeState pause() override;
  contracts::runtime::RuntimeState resume() override;
  contracts::runtime::CartridgeInsertionResult insertCartridge(contracts::runtime::CartridgeInsertion insertion) override;
  contracts::runtime::RuntimeState ejectCartridge() override;
  contracts::runtime::RuntimeState bootMachine(contracts::runtime::MachineBoot boot) override;
  bool useMachine(const std::function<void(contracts::hardware::Machine&)>& action) override;
  void runOnSimulation(const std::function<void()>& action) override;
  void sendInput(contracts::hardware::InputEvent event) override;
  void setInputHook(std::shared_ptr<contracts::runtime::InputHook> hook) override;
  void setMachineRomSource(contracts::runtime::MachineRomSource source) override;
  void setFamilyKeyboard(bool connected) override;
  void setHideStartupFrames(bool hide) override;
  void setFrameEventInterval(std::uint32_t frames) override;
  std::shared_future<contracts::runtime::RunOutcome> startRun(contracts::runtime::RunQuota quota) override;
  contracts::runtime::SpeedSetting setSpeed(contracts::runtime::SpeedSetting speed) override;
  void cancelRun() override;
  unsigned padPortCount() override;
  void setPadButtons(unsigned port, std::uint8_t buttons) override;
  contracts::runtime::FrameImage captureFrame() override;
  contracts::runtime::StepOutcome step(contracts::hardware::StepUnit unit, std::int64_t count) override;
  void inspect(const std::function<void(contracts::runtime::DebugContext&)>& action) override;
  void setObserver(std::function<void(contracts::runtime::DebugContext&)> observer) override;

 private:
  /** 正在进行的授权运行（只由模拟线程访问）。 */
  struct ActiveRun {
    contracts::runtime::RunUnit unit{contracts::runtime::RunUnit::Frames};
    std::int64_t total{0};
    std::int64_t remaining{0};
    std::uint64_t startCycles{0};
    std::uint64_t startFrame{0};
    /** 按周期、指令、扫描线授权时已执行的指令条数。 */
    std::int64_t instructions{0};
    std::shared_ptr<std::promise<contracts::runtime::RunOutcome>> promise;
  };

  /** 队列里的命令；cancelled 为 true 表示运行服务已停止，命令不再执行。 */
  using Command = std::function<void(bool cancelled)>;
  using Clock = std::chrono::steady_clock;

  /** 把命令放进队列；已停止时抛出 core.unavailable。 */
  void post(Command command);
  /** 投递一条命令并等它在模拟线程执行完，返回它的结果，命令里的异常原样抛给调用方。 */
  template <typename Function>
  auto submit(Function function) -> std::invoke_result_t<Function&>;

  void threadMain();
  /** 以下方法只在模拟线程调用。 */
  void runPending(std::unique_lock<std::mutex>& lock);
  void runFrame();
  /** 按周期、指令、扫描线授权的运行：跑一小块，然后像一帧结束那样发布画面与状态。 */
  void runQuotaChunk();
  /** 运行速度是 100% 且不限速。 */
  [[nodiscard]] bool normalSpeed() const { return !maxSpeed_ && speedPercent_ == 100; }
  [[nodiscard]] bool powered() const;
  [[nodiscard]] bool running() const { return running_ && powered(); }
  [[nodiscard]] contracts::runtime::RuntimeState state() const;
  void startMachine(bool resume);
  void stopMachine();
  /** 整机机型（没有卡带）的 ROM 与设置里的不同时，换成按新 ROM 装配的主机（不通电）；换了返回 true；读不到新 ROM 时抛出，原主机不变。 */
  bool refreshMachineRom();
  void publishState(const char* reason);
  void publishSnapshot();
  /** 机器现在是否带鼠标：主机自带，或卡带的 mapper 插件声明了 mouse 外设；没有主机时为 false。 */
  [[nodiscard]] bool mouseAvailable() const;
  void reportFailure(const std::string& module, const std::string& code, const std::string& message);
  /** 结束正在进行的授权运行：把结果交给等待的调用方并发 runtime.runFinished；没有授权运行时没有效果；不改变电源与运行模式。 */
  void finishRun(const char* stopped);
  /** 命中断点：结束授权运行（breakpoint）、暂停、发 runtime.breakpointHit 与状态变化。 */
  void handleBreakpoint(const contracts::hardware::BreakpointHit& hit);
  void checkCartridge();
  /** 主机通电时调用观察者；回调抛出的异常写日志。 */
  void notifyObserver();
  void pushAudio();
  /** 把一帧交给帧输出；开机与复位后的最初几帧在开启遮黑时换成黑色。 */
  void publishPicture(const contracts::hardware::FrameBuffer& picture);
  void syncAudio();
  /** 换了主机之后，按设置与主机的帧率更新帧节拍。 */
  void updateFramePeriod();

  SimulationRuntimeOptions options_;
  std::chrono::nanoseconds framePeriod_{0};

  std::once_flag stopOnce_;
  std::mutex mutex_;
  std::condition_variable wake_;
  std::deque<Command> commands_;
  bool stopping_{false};

  mutable std::mutex snapshotMutex_;
  contracts::runtime::RuntimeStatusResult snapshot_;

  std::atomic<std::uint32_t> frameEventInterval_{0};

  // 以下只由模拟线程访问：硬件状态的唯一拥有者。卡带先于主机销毁。
  std::unique_ptr<contracts::hardware::Machine> machine_;
  /** 输入钩子；只在模拟线程访问。 */
  std::shared_ptr<contracts::runtime::InputHook> inputHook_;
  /** 经典 FC 是否接着 Family BASIC 键盘；新装配的主机按它设置。 */
  bool familyKeyboard_{false};
  /** 开机与复位后是否遮黑最初的几帧（任意线程设置，模拟线程在上电与复位时读取）。 */
  std::atomic<bool> hideStartupFrames_{false};
  /** 还要遮黑的帧数，只由模拟线程访问。 */
  int maskFramesLeft_{0};
  /** 遮黑用的整帧黑色像素（不透明），按需要的尺寸重新填充。 */
  std::vector<std::uint32_t> blackFrame_;
  std::unique_ptr<contracts::mappers::MapperCartridge> cartridge_;
  std::optional<contracts::runtime::CartridgeSummary> summary_;
  std::string host_;
  /** 整机机型开机所用 ROM 的版本；装入卡带、取出时清空。 */
  std::string romLabel_;
  /** 整机机型开机所用 ROM 的内容，用来判断设置里的 ROM 是否换过；装入卡带、取出时清空。 */
  std::vector<std::uint8_t> machineRom_;
  /** 机型 ROM 的来源；只在模拟线程访问。 */
  contracts::runtime::MachineRomSource machineRomSource_;
  /** 最近一次发布的快照里机器是否带鼠标，用来发现运行中的变化（如学习机进入 FC 兼容模式）。 */
  bool mouse_{false};
  bool running_{false};
  /** 运行速度（百分比）与是否不限速，只由模拟线程访问。 */
  std::int64_t speedPercent_{100};
  bool maxSpeed_{false};
  /** 正在进行的授权运行。 */
  std::optional<ActiveRun> run_;
  /** 要重新对齐帧节拍（刚开始运行，或落后太多）。 */
  bool resync_{true};
  bool failureReported_{false};
  /** 观察者（见 RuntimeService::setObserver）。 */
  std::function<void(contracts::runtime::DebugContext&)> observer_;
  std::uint64_t frame_{0};
  /** 从主机取出的采样，反复使用以免每帧分配内存。 */
  std::vector<float> audioBuffer_;
  /** 声音输出当前被设为“在播放”。 */
  bool audioPlaying_{false};

  /** 必须最后声明：线程在构造函数末尾启动，析构时先于其他成员停止。 */
  std::thread thread_;
};

}  // namespace ember::logic::runtime
