/*
 * ------------------------------------------------------------------------
 * 名称：core.hpp
 * 说明：核心入口：启动（创建日志、登记全部模块）、在线程池中处理请求、取消请求、转发事件、停止。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-13
 * 备注：参数与结果都是 JSON 文本；错误以 {"code","message"} 返回，异常不越过本类。binding 只经本类使用核心。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "platform/streams/frame-output.hpp"

namespace ember::contracts::runtime {
class RuntimeService;
}  // namespace ember::contracts::runtime

namespace ember::platform {
class CancellationToken;
class ErrorLog;
class RequestRouter;
class ThreadPool;
}  // namespace ember::platform

namespace ember::bootstrap {

class CoreEvents;
class CoreFrames;

/** 核心入口。 */
class Core {
 public:
  /** 请求完成：ok 为 true 时 payload 是结果 JSON，否则是 {"code","message"}。可能在任意线程调用。 */
  using RequestDone = std::function<void(bool ok, std::string payload)>;
  /** 事件：事件名与数据 JSON。可能在任意线程调用。 */
  using EventHandler = std::function<void(const std::string& name, const std::string& dataJson)>;
  /** 帧：每完成一帧在模拟线程调用，像素只在调用期间有效，处理函数不能阻塞。 */
  using FrameHandler = std::function<void(const platform::FrameView& frame)>;

  Core();
  ~Core();

  Core(const Core&) = delete;
  Core& operator=(const Core&) = delete;

  /**
   * 启动核心。
   * @param optionsJson {"userDataPath": 用户数据目录, "appVersion": 应用版本, "workerCount"?: 工作线程数, "bundledPluginsPath"?: 预装的 mapper 插件目录}。
   * @returns {"coreVersion", "requests": [全部请求名]} 的 JSON 文本。
   * @throws shared::CoreError 参数不合格或已启动；std::exception 创建日志等失败。
   */
  std::string start(const std::string& optionsJson);

  /** 设置事件处理函数；应在 start 之前调用。 */
  void setEventHandler(EventHandler handler);

  /** 设置帧处理函数；应在 start 之前调用，没有处理函数时画面被丢弃。 */
  void setFrameHandler(FrameHandler handler);

  /**
   * 把用户输入（键盘、鼠标、游戏手柄的原始事件）校验后经命令队列交给模拟线程，不等待执行。
   * @param messageJson 消息的 JSON 文本，如 {"type":"key","code":"KeyW","down":true}，格式见 input-message.hpp。
   * @throws shared::CoreError core.invalidParams：消息不合格；core.unavailable：核心未启动或正在停止。
   */
  void sendInput(const std::string& messageJson);

  /** 处理请求；结果经 done 返回，未启动或已停止时立即以 core.unavailable 返回。 */
  void request(const std::string& id, const std::string& name, const std::string& paramsJson, RequestDone done);

  /** 取消进行中的请求；请求已结束时忽略。 */
  void cancel(const std::string& id);

  /** 停止接受请求，等待进行中的请求结束，关闭日志。 */
  void stop();

 private:
  void run(const std::string& id, const std::string& name, const std::string& paramsJson, const platform::CancellationToken& token, const RequestDone& done);

  std::mutex mutex_;
  bool started_ = false;
  bool stopping_ = false;
  std::map<std::string, std::shared_ptr<platform::CancellationToken>> running_;
  std::shared_ptr<CoreEvents> events_;
  std::shared_ptr<CoreFrames> frames_;
  std::shared_ptr<contracts::runtime::RuntimeService> runtime_;
  std::shared_ptr<platform::ErrorLog> errors_;
  std::unique_ptr<platform::RequestRouter> router_;
  std::unique_ptr<platform::ThreadPool> pool_;
  std::vector<std::function<void()>> stopHandlers_;
};

}  // namespace ember::bootstrap
