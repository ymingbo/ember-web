/*
 * ------------------------------------------------------------------------
 * 名称：script-runtime.cpp
 * 说明：mapper 脚本沙箱的实现：QuickJS-ng 运行时、宿主对象 ctx、脚本加载与回调、执行限制与错误分类。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * 备注：只加载确定性的内置对象（没有 Date、没有 quickjs-libc），并删除 Math.random；时长限制由中断处理函数按时间判断，
 *       超时的中止是脚本不能捕获的；C 回调里的 C++ 异常都在 guarded 里截获，绝不穿过 QuickJS 的 C 栈帧。
 *       JSValue 的引用计数：JS_Set*、JS_Define*GetSet 接管传入的值，其余返回值都要由调用方释放。
 * ------------------------------------------------------------------------
 */

#include "infra/mappers/script-runtime.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

#include <quickjs.h>

#include "contracts/mappers/mapper-error.hpp"
#include "shared/errors/core-error.hpp"

namespace ember::infra::mappers {

namespace {

namespace mapper_error = contracts::mappers;
namespace hw = contracts::hardware;

/** 宿主函数的编号，经 JS 函数的 magic 传入。 */
enum Operation : int {
  WindowSetBank,
  WindowSetOffset,
  WindowUnmap,
  WindowBankCount,
  WindowSelect,
  WindowSetAccess,
  BusConflictSet,
  CpuPeek,
  Random,
  StorageRead,
  StorageWrite,
  StateGet,
  StateSet,
  IrqAssert,
  IrqRelease,
  PadSet,
  PadRelease,
  TapeLevel,
  TapeSetComparator,
  Cycle,
  TimerAfter,
  TimerCancel,
  CircuitWrite,
  CircuitGet,
  CircuitBank,
  NametableMap
};

/** JS 数字能精确表示的最大整数。 */
constexpr double MAX_SAFE_INTEGER = 9007199254740991.0;

/** ctx.timer.after 的周期数上限（2^40）。 */
constexpr std::int64_t MAX_TIMER_CYCLES = std::int64_t{1} << 40;

bool contains(const std::string& text, const char* part) { return text.find(part) != std::string::npos; }

}  // namespace

struct ScriptRuntime::Impl {
  Impl(std::string id, ScriptLayout scriptLayout, ScriptBindings& scriptBindings, ScriptLimits scriptLimits)
      : pluginId(std::move(id)), layout(std::move(scriptLayout)), bindings(scriptBindings), limits(scriptLimits) {}

  ~Impl() {
    if (js != nullptr) {
      for (const JSValue value : windowObjects) JS_FreeValue(js, value);
      for (const JSValue value : storageObjects) JS_FreeValue(js, value);
      for (const JSValue value : padObjects) JS_FreeValue(js, value);
      for (const JSValue value : circuitObjects) JS_FreeValue(js, value);
      JS_FreeValue(js, settingValues);
      for (const JSValue value : {hostObject, plugin, resetFn, readFn, writeFn, keyFn, mouseFn, tapeEdgeFn, timerFn, circuitFn, a12Fn, tablesObject, tapeObject, cpuName, ppuName, powerName, softName}) JS_FreeValue(js, value);
      JS_FreeContext(js);
    }
    if (runtime != nullptr) JS_FreeRuntime(runtime);
  }

  Impl(const Impl&) = delete;
  Impl& operator=(const Impl&) = delete;

  std::string pluginId;
  ScriptLayout layout;
  ScriptBindings& bindings;
  ScriptLimits limits;

  JSRuntime* runtime{nullptr};
  JSContext* js{nullptr};
  JSValue hostObject = JS_UNDEFINED;
  JSValue plugin = JS_UNDEFINED;
  JSValue resetFn = JS_UNDEFINED;
  JSValue readFn = JS_UNDEFINED;
  JSValue writeFn = JS_UNDEFINED;
  JSValue keyFn = JS_UNDEFINED;
  JSValue mouseFn = JS_UNDEFINED;
  JSValue tapeEdgeFn = JS_UNDEFINED;
  JSValue timerFn = JS_UNDEFINED;
  JSValue circuitFn = JS_UNDEFINED;
  JSValue a12Fn = JS_UNDEFINED;
  /** 脚本导出的 tables 对象（按查表标识放计算函数）；没有时为 undefined。 */
  JSValue tablesObject = JS_UNDEFINED;
  /** ctx.device('tape') 返回的对象；主机没有磁带机时为 undefined。 */
  JSValue tapeObject = JS_UNDEFINED;
  JSValue cpuName = JS_UNDEFINED;
  JSValue ppuName = JS_UNDEFINED;
  JSValue powerName = JS_UNDEFINED;
  JSValue softName = JS_UNDEFINED;
  std::vector<JSValue> windowObjects;
  std::vector<JSValue> storageObjects;
  std::vector<JSValue> padObjects;
  std::vector<JSValue> circuitObjects;
  /** 设置项的当前值（key 到值），ctx.settings.get 从这里取。 */
  JSValue settingValues = JS_UNDEFINED;
  std::chrono::steady_clock::time_point deadline;
  bool timedOut{false};

  // ------------------------------------------------------------ 创建

  /** 创建运行时与上下文，建立 ctx，加载脚本。失败时抛出异常，已创建的部分由析构释放。 */
  void initialize(std::string_view source) {
    armDeadline();
    runtime = JS_NewRuntime();
    if (runtime == nullptr) throw shared::CoreError(mapper_error::ERROR_LIMIT_EXCEEDED, "无法创建脚本运行时：内存不足");
    JS_SetMemoryLimit(runtime, limits.memoryBytes);
    JS_SetMaxStackSize(runtime, limits.stackBytes);
    JS_SetInterruptHandler(runtime, &Impl::interrupt, this);

    js = JS_NewContextRaw(runtime);
    if (js == nullptr) throw shared::CoreError(mapper_error::ERROR_LIMIT_EXCEEDED, "无法创建脚本上下文：内存不足");
    JS_SetContextOpaque(js, this);
    if (JS_AddIntrinsicBaseObjects(js) < 0 || JS_AddIntrinsicEval(js) < 0) throw shared::CoreError(mapper_error::ERROR_LIMIT_EXCEEDED, "无法初始化脚本的内置对象：内存不足");
    JS_AddIntrinsicRegExpCompiler(js);
    JS_AddIntrinsicRegExp(js);
    JS_AddIntrinsicJSON(js);
    JS_AddIntrinsicProxy(js);
    JS_AddIntrinsicMapSet(js);
    JS_AddIntrinsicTypedArrays(js);
    JS_AddIntrinsicPromise(js);
    JS_AddIntrinsicBigInt(js);
    removeMathRandom();

    cpuName = JS_NewString(js, "cpu");
    ppuName = JS_NewString(js, "ppu");
    powerName = JS_NewString(js, "power");
    softName = JS_NewString(js, "soft");
    buildHostObject();
    loadModule(source);
  }

  /** 脚本不能有随机性：同样的输入必须得到同样的结果。 */
  void removeMathRandom() {
    const JSValue global = JS_GetGlobalObject(js);
    const JSValue math = JS_GetPropertyStr(js, global, "Math");
    if (JS_IsObject(math)) {
      const JSAtom atom = JS_NewAtom(js, "random");
      JS_DeleteProperty(js, math, atom, 0);
      JS_FreeAtom(js, atom);
    }
    JS_FreeValue(js, math);
    JS_FreeValue(js, global);
  }

  /** 给对象加一个宿主函数（带数据的 C 函数，magic 区分操作，data 是窗口、存储区或状态的编号）。 */
  void addFunction(JSValueConst object, const char* name, JSCFunctionData* function, int length, int magic, std::size_t index) {
    JSValue data = JS_NewInt32(js, static_cast<int32_t>(index));
    JS_SetPropertyStr(js, object, name, JS_NewCFunctionData2(js, function, name, length, magic, 1, &data));
  }

  void buildHostObject() {
    hostObject = JS_NewObject(js);

    JSValue header = JS_ParseJSON(js, layout.headerJson.c_str(), layout.headerJson.size(), "header");
    if (JS_IsException(header)) throw describeFailure("初始化 ctx.header", false);
    JS_FreezeObject(js, header);
    JS_SetPropertyStr(js, hostObject, "header", header);

    JS_SetPropertyStr(js, hostObject, "window", JS_NewCFunction(js, &Impl::lookupWindow, "window", 1));
    JS_SetPropertyStr(js, hostObject, "storage", JS_NewCFunction(js, &Impl::lookupStorage, "storage", 1));

    JSValue nametable = JS_NewObject(js);
    JS_SetPropertyStr(js, nametable, "set", JS_NewCFunction(js, &Impl::nametableSet, "set", 1));
    addFunction(nametable, "map", &Impl::hostCall, 3, NametableMap, 0);
    JS_SetPropertyStr(js, hostObject, "nametable", nametable);

    JSValue irq = JS_NewObject(js);
    addFunction(irq, "assert", &Impl::hostCall, 0, IrqAssert, 0);
    addFunction(irq, "release", &Impl::hostCall, 0, IrqRelease, 0);
    JS_SetPropertyStr(js, hostObject, "irq", irq);

    JSValue busConflict = JS_NewObject(js);
    addFunction(busConflict, "set", &Impl::hostCall, 1, BusConflictSet, 0);
    JS_SetPropertyStr(js, hostObject, "busConflict", busConflict);
    addFunction(hostObject, "peek", &Impl::hostCall, 1, CpuPeek, 0);
    addFunction(hostObject, "random", &Impl::hostCall, 1, Random, 0);
    addFunction(hostObject, "cycle", &Impl::hostCall, 0, Cycle, 0);
    JSValue timer = JS_NewObject(js);
    addFunction(timer, "after", &Impl::hostCall, 2, TimerAfter, 0);
    addFunction(timer, "cancel", &Impl::hostCall, 1, TimerCancel, 0);
    JS_SetPropertyStr(js, hostObject, "timer", timer);

    JSValue log = JS_NewObject(js);
    JS_SetPropertyStr(js, log, "warn", JS_NewCFunction(js, &Impl::logWarn, "warn", 1));
    JS_SetPropertyStr(js, hostObject, "log", log);

    JSValue keys = JS_NewObject(js);
    JS_SetPropertyStr(js, keys, "isDown", JS_NewCFunction(js, &Impl::keysIsDown, "isDown", 1));
    JS_SetPropertyStr(js, hostObject, "keys", keys);
    JS_SetPropertyStr(js, hostObject, "pad", JS_NewCFunction(js, &Impl::lookupPad, "pad", 1));
    JS_SetPropertyStr(js, hostObject, "device", JS_NewCFunction(js, &Impl::lookupDevice, "device", 1));
    JS_SetPropertyStr(js, hostObject, "circuit", JS_NewCFunction(js, &Impl::lookupCircuit, "circuit", 1));
    settingValues = JS_NewObject(js);
    for (const ScriptSetting& setting : layout.settings) {
      JS_SetPropertyStr(js, settingValues, setting.key.c_str(), setting.isSwitch ? JS_NewBool(js, setting.flag) : JS_NewStringLen(js, setting.text.data(), setting.text.size()));
    }
    JSValue settings = JS_NewObject(js);
    JS_SetPropertyStr(js, settings, "get", JS_NewCFunction(js, &Impl::settingsGet, "get", 1));
    JS_SetPropertyStr(js, hostObject, "settings", settings);
    for (std::size_t index = 0; index < layout.circuits.size(); ++index) {
      JSValue circuit = JS_NewObject(js);
      addFunction(circuit, "write", &Impl::hostCall, 2, CircuitWrite, index);
      addFunction(circuit, "get", &Impl::hostCall, 1, CircuitGet, index);
      addFunction(circuit, "bank", &Impl::hostCall, 2, CircuitBank, index);
      JS_FreezeObject(js, circuit);
      circuitObjects.push_back(circuit);
    }
    if (layout.hasTape) {
      tapeObject = JS_NewObject(js);
      addFunction(tapeObject, "level", &Impl::hostCall, 0, TapeLevel, 0);
      addFunction(tapeObject, "setComparator", &Impl::hostCall, 3, TapeSetComparator, 0);
      JS_FreezeObject(js, tapeObject);
    }
    for (std::size_t port = 0; port < layout.padPorts; ++port) {
      JSValue pad = JS_NewObject(js);
      addFunction(pad, "set", &Impl::hostCall, 2, PadSet, port);
      addFunction(pad, "release", &Impl::hostCall, 0, PadRelease, port);
      JS_FreezeObject(js, pad);
      padObjects.push_back(pad);
    }

    JSValue state = JS_NewObject(js);
    for (std::size_t index = 0; index < layout.states.size(); ++index) {
      JSValue data = JS_NewInt32(js, static_cast<int32_t>(index));
      JSValue getter = JS_NewCFunctionData2(js, &Impl::hostCall, "get", 0, StateGet, 1, &data);
      JSValue setter = JS_NewCFunctionData2(js, &Impl::hostCall, "set", 1, StateSet, 1, &data);
      const JSAtom atom = JS_NewAtom(js, layout.states[index].c_str());
      JS_DefinePropertyGetSet(js, state, atom, getter, setter, JS_PROP_ENUMERABLE);
      JS_FreeAtom(js, atom);
    }
    JS_PreventExtensions(js, state);
    JS_SetPropertyStr(js, hostObject, "state", state);

    for (std::size_t index = 0; index < layout.windows.size(); ++index) {
      JSValue window = JS_NewObject(js);
      addFunction(window, "setBank", &Impl::hostCall, 1, WindowSetBank, index);
      addFunction(window, "setOffset", &Impl::hostCall, 1, WindowSetOffset, index);
      addFunction(window, "unmap", &Impl::hostCall, 0, WindowUnmap, index);
      addFunction(window, "select", &Impl::hostCall, 2, WindowSelect, index);
      addFunction(window, "setAccess", &Impl::hostCall, 1, WindowSetAccess, index);
      JSValue countData = JS_NewInt32(js, static_cast<int32_t>(index));
      JSValue countGetter = JS_NewCFunctionData2(js, &Impl::hostCall, "bankCount", 0, WindowBankCount, 1, &countData);
      const JSAtom countAtom = JS_NewAtom(js, "bankCount");
      JS_DefinePropertyGetSet(js, window, countAtom, countGetter, JS_UNDEFINED, JS_PROP_ENUMERABLE);
      JS_FreeAtom(js, countAtom);
      JS_FreezeObject(js, window);
      windowObjects.push_back(window);
    }
    for (std::size_t index = 0; index < layout.storages.size(); ++index) {
      JSValue storage = JS_NewObject(js);
      JS_SetPropertyStr(js, storage, "size", JS_NewInt64(js, static_cast<int64_t>(bindings.storageSize(index))));
      addFunction(storage, "read", &Impl::hostCall, 1, StorageRead, index);
      addFunction(storage, "write", &Impl::hostCall, 2, StorageWrite, index);
      JS_FreezeObject(js, storage);
      storageObjects.push_back(storage);
    }
    JS_FreezeObject(js, hostObject);
  }

  /** 加载脚本：编译为模块并求值，取默认导出的对象和它的 reset、read、write。 */
  void loadModule(std::string_view source) {
    const std::string code(source);
    armDeadline();
    JSValue compiled = JS_Eval(js, code.c_str(), code.size(), "main.js", JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
    if (JS_IsException(compiled)) throw describeFailure("编译", true);
    JSModuleDef* module = static_cast<JSModuleDef*>(JS_VALUE_GET_PTR(compiled));
    JSValue evaluated = JS_EvalFunction(js, compiled);
    if (JS_IsException(evaluated)) throw describeFailure("加载", true);
    if (JS_IsPromise(evaluated)) {
      const JSPromiseStateEnum state = JS_PromiseState(js, evaluated);
      if (state == JS_PROMISE_REJECTED) {
        JSValue reason = JS_PromiseResult(js, evaluated);
        const std::string text = describeValue(reason);
        JS_FreeValue(js, reason);
        JS_FreeValue(js, evaluated);
        throw classifyFailure(text, "加载", true);
      }
      if (state == JS_PROMISE_PENDING) {
        JS_FreeValue(js, evaluated);
        throw shared::CoreError(mapper_error::ERROR_INVALID_PACKAGE, "插件 " + pluginId + " 的 main.js 不能使用顶层 await");
      }
    }
    JS_FreeValue(js, evaluated);

    JSValue names = JS_GetModuleNamespace(js, module);
    JSValue exported = JS_GetPropertyStr(js, names, "default");
    JS_FreeValue(js, names);
    if (!JS_IsObject(exported)) {
      JS_FreeValue(js, exported);
      throw shared::CoreError(mapper_error::ERROR_INVALID_PACKAGE, "插件 " + pluginId + " 的 main.js 必须用 export default 导出一个对象");
    }
    plugin = exported;
    resetFn = readHandler("reset");
    readFn = readHandler("read");
    writeFn = readHandler("write");
    keyFn = readHandler("onKey");
    mouseFn = readHandler("onMouse");
    tapeEdgeFn = readHandler("onTapeEdge");
    timerFn = readHandler("onTimer");
    circuitFn = readHandler("onCircuit");
    a12Fn = readHandler("onA12");
    tablesObject = JS_GetPropertyStr(js, plugin, "tables");
    if (JS_IsException(tablesObject)) throw describeFailure("读取导出的 tables", true);
    if (!JS_IsUndefined(tablesObject) && !JS_IsObject(tablesObject)) throw shared::CoreError(mapper_error::ERROR_INVALID_PACKAGE, "插件 " + pluginId + " 导出的 tables 必须是对象");
  }

  JSValue readHandler(const char* name) {
    JSValue handler = JS_GetPropertyStr(js, plugin, name);
    if (JS_IsException(handler)) throw describeFailure("读取导出的函数", true);
    if (!JS_IsUndefined(handler) && !JS_IsFunction(js, handler)) {
      JS_FreeValue(js, handler);
      throw shared::CoreError(mapper_error::ERROR_INVALID_PACKAGE, "插件 " + pluginId + " 导出的 " + name + " 必须是函数");
    }
    return handler;
  }

  // ------------------------------------------------------------ 限制与错误

  void armDeadline() {
    timedOut = false;
    deadline = std::chrono::steady_clock::now() + limits.callTimeout;
  }

  /** 中断处理函数：超过期限就中止脚本，这种中止不能被脚本捕获。 */
  static int interrupt(JSContext*, void* opaque) {
    auto* owner = static_cast<Impl*>(opaque);
    if (std::chrono::steady_clock::now() < owner->deadline) return 0;
    owner->timedOut = true;
    return 1;
  }

  /** 取字符串属性，没有或不是字符串时为空。 */
  std::string stringProperty(JSValueConst object, const char* key) {
    const JSValue value = JS_GetPropertyStr(js, object, key);
    std::string text;
    if (JS_IsString(value)) {
      std::size_t length = 0;
      if (const char* chars = JS_ToCStringLen(js, &length, value)) {
        text.assign(chars, length);
        JS_FreeCString(js, chars);
      }
    }
    JS_FreeValue(js, value);
    return text;
  }

  /** 异常或抛出值的文字：Error 取“名称: 消息”与第一个调用栈位置，其他值转成字符串。 */
  std::string describeValue(JSValueConst value) {
    if (JS_IsError(value)) {
      const std::string name = stringProperty(value, "name");
      const std::string message = stringProperty(value, "message");
      std::string text = name.empty() ? message : name + ": " + message;
      const std::string stack = stringProperty(value, "stack");
      const std::size_t begin = stack.find_first_not_of(" \t\r\n");
      if (begin != std::string::npos) text += " [" + stack.substr(begin, stack.find_first_of("\r\n", begin) - begin) + "]";
      return text;
    }
    std::size_t length = 0;
    if (const char* chars = JS_ToCStringLen(js, &length, value)) {
      std::string text(chars, length);
      JS_FreeCString(js, chars);
      return text;
    }
    JS_FreeValue(js, JS_GetException(js));
    return "抛出了无法转成文字的值";
  }

  /**
   * 取出当前的异常并转成 CoreError：超时、内存或栈超限为 mappers.limitExceeded；
   * 否则加载阶段为 mappers.invalidPackage，运行阶段为 mappers.scriptError。
   */
  shared::CoreError describeFailure(const char* phase, bool loading) {
    const JSValue exception = JS_GetException(js);
    const std::string text = describeValue(exception);
    JS_FreeValue(js, exception);
    return classifyFailure(text, phase, loading);
  }

  /** 按错误文字与超时标志分类，规则见 describeFailure。 */
  shared::CoreError classifyFailure(const std::string& text, const char* phase, bool loading) const {
    const std::string prefix = "插件 " + pluginId + " 的脚本";
    if (timedOut) return shared::CoreError(mapper_error::ERROR_LIMIT_EXCEEDED, prefix + "在 " + phase + " 时执行超过 " + std::to_string(limits.callTimeout.count()) + " 毫秒，已中止");
    // 内存耗尽到连错误对象都建不出来时，QuickJS 抛出的是 null。
    if (contains(text, "out of memory") || text == "null") {
      return shared::CoreError(mapper_error::ERROR_LIMIT_EXCEEDED, prefix + "在 " + phase + " 时超出 " + std::to_string(limits.memoryBytes / (1024 * 1024)) + " MB 的内存上限");
    }
    if (contains(text, "Maximum call stack")) return shared::CoreError(mapper_error::ERROR_LIMIT_EXCEEDED, prefix + "在 " + phase + " 时调用栈超出上限");
    if (loading) return shared::CoreError(mapper_error::ERROR_INVALID_PACKAGE, "插件 " + pluginId + " 的 main.js 无法" + phase + "：" + text);
    return shared::CoreError(mapper_error::ERROR_SCRIPT_ERROR, prefix + "在 " + phase + " 时出错：" + text);
  }

  // ------------------------------------------------------------ 回调

  JSValue invoke(const char* phase, JSValueConst function, int argc, JSValue* argv) {
    JS_UpdateStackTop(runtime);
    armDeadline();
    const JSValue result = JS_Call(js, function, plugin, argc, argv);
    if (JS_IsException(result)) throw describeFailure(phase, false);
    return result;
  }

  /** 调用输入回调 function(ctx, event)；event 的引用由本函数释放。 */
  void invokeInput(const char* phase, JSValueConst function, JSValue event) {
    if (JS_IsException(event)) throw describeFailure("构造输入事件", false);
    JSValue arguments[2] = {hostObject, event};
    JSValue result = JS_UNDEFINED;
    try {
      result = invoke(phase, function, 2, arguments);
    } catch (...) {
      JS_FreeValue(js, event);
      throw;
    }
    JS_FreeValue(js, event);
    JS_FreeValue(js, result);
  }

  JSValue keyEvent(const hw::KeyInput& input) {
    JSValue event = JS_NewObject(js);
    JS_SetPropertyStr(js, event, "code", JS_NewStringLen(js, input.code.data(), input.code.size()));
    JS_SetPropertyStr(js, event, "down", JS_NewBool(js, input.down ? 1 : 0));
    return event;
  }

  JSValue mouseEvent(const hw::MouseInput& input) {
    const char* kind = "move";
    switch (input.kind) {
      case hw::MouseKind::Move:
        break;
      case hw::MouseKind::Down:
        kind = "down";
        break;
      case hw::MouseKind::Up:
        kind = "up";
        break;
      case hw::MouseKind::Wheel:
        kind = "wheel";
        break;
      case hw::MouseKind::Leave:
        kind = "leave";
        break;
    }
    JSValue event = JS_NewObject(js);
    JS_SetPropertyStr(js, event, "kind", JS_NewString(js, kind));
    JS_SetPropertyStr(js, event, "x", JS_NewFloat64(js, input.x));
    JS_SetPropertyStr(js, event, "y", JS_NewFloat64(js, input.y));
    JS_SetPropertyStr(js, event, "dx", JS_NewFloat64(js, input.dx));
    JS_SetPropertyStr(js, event, "dy", JS_NewFloat64(js, input.dy));
    JS_SetPropertyStr(js, event, "buttons", JS_NewInt32(js, input.buttons));
    JS_SetPropertyStr(js, event, "wheel", JS_NewFloat64(js, input.wheel));
    return event;
  }

  // ------------------------------------------------------------ 宿主函数（C 回调）

  static Impl* self(JSContext* context) { return static_cast<Impl*>(JS_GetContextOpaque(context)); }

  /** 在 C 回调里执行 body：宿主抛出的异常转成脚本里的异常，不让 C++ 异常穿过 QuickJS。 */
  template <typename Body>
  static JSValue guarded(JSContext* context, Body&& body) {
    try {
      return body();
    } catch (const ScriptArgumentError& error) {
      return JS_ThrowRangeError(context, "%s", error.what());
    } catch (const std::exception& error) {
      return JS_ThrowInternalError(context, "宿主内部错误：%s", error.what());
    } catch (...) {
      return JS_ThrowInternalError(context, "宿主内部错误");
    }
  }

  static JSValue argument(int argc, JSValueConst* argv, int index) { return index < argc ? argv[index] : JS_UNDEFINED; }

  /** 要求参数是整数（可以带小数点但值为整数）。 */
  static std::int64_t integerArgument(JSContext* context, JSValueConst value, const char* name) {
    if (!JS_IsNumber(value)) throw ScriptArgumentError(std::string("参数 ") + name + " 必须是数字");
    double number = 0;
    JS_ToFloat64(context, &number, value);
    if (!std::isfinite(number) || std::trunc(number) != number || std::fabs(number) > MAX_SAFE_INTEGER) throw ScriptArgumentError(std::string("参数 ") + name + " 必须是整数");
    return static_cast<std::int64_t>(number);
  }

  /** 取字符串参数；不是字符串时返回 false。 */
  static bool stringArgument(JSContext* context, JSValueConst value, std::string& out) {
    if (!JS_IsString(value)) return false;
    std::size_t length = 0;
    const char* chars = JS_ToCStringLen(context, &length, value);
    if (chars == nullptr) return false;
    out.assign(chars, length);
    JS_FreeCString(context, chars);
    return true;
  }

  /** 带编号的宿主函数：窗口、存储区、状态与中断引脚的操作。 */
  static JSValue hostCall(JSContext* context, JSValueConst, int argc, JSValueConst* argv, int magic, JSValueConst* data) {
    Impl* runtimeSelf = self(context);
    return guarded(context, [&]() -> JSValue {
      const std::size_t index = static_cast<std::size_t>(JS_VALUE_GET_INT(data[0]));
      ScriptBindings& host = runtimeSelf->bindings;
      switch (magic) {
        case WindowSetBank:
          host.windowSetBank(index, integerArgument(context, argument(argc, argv, 0), "bank"));
          return JS_UNDEFINED;
        case WindowSetOffset:
          host.windowSetOffset(index, integerArgument(context, argument(argc, argv, 0), "bytes"));
          return JS_UNDEFINED;
        case WindowUnmap:
          host.windowUnmap(index);
          return JS_UNDEFINED;
        case WindowBankCount:
          return JS_NewUint32(context, static_cast<std::uint32_t>(host.windowBankCount(index)));
        case WindowSelect: {
          std::string storageId;
          if (!stringArgument(context, argument(argc, argv, 0), storageId)) throw ScriptArgumentError("参数 storage 必须是字符串");
          const auto& names = runtimeSelf->layout.storages;
          const auto found = std::find(names.begin(), names.end(), storageId);
          if (found == names.end()) throw ScriptArgumentError("存储区不存在：" + storageId);
          const std::int64_t bank = argc > 1 ? integerArgument(context, argv[1], "bank") : 0;
          host.windowSelect(index, static_cast<std::size_t>(found - names.begin()), bank);
          return JS_UNDEFINED;
        }
        case WindowSetAccess: {
          std::string mode;
          if (!stringArgument(context, argument(argc, argv, 0), mode) || (mode != "r" && mode != "rw")) throw ScriptArgumentError("参数 access 必须是 'r' 或 'rw'");
          host.windowSetAccess(index, mode == "rw");
          return JS_UNDEFINED;
        }
        case BusConflictSet: {
          const JSValue enabled = argument(argc, argv, 0);
          if (!JS_IsBool(enabled)) throw ScriptArgumentError("参数 enabled 必须是布尔值");
          host.busConflictSet(JS_ToBool(context, enabled) != 0);
          return JS_UNDEFINED;
        }
        case Random: {
          const std::int64_t max = integerArgument(context, argument(argc, argv, 0), "max");
          if (max < 1 || max > 0x80000000LL) throw ScriptArgumentError("参数 max 必须在 1 到 2147483648 之间");
          return JS_NewUint32(context, host.randomBelow(static_cast<std::uint32_t>(max)));
        }
        case CpuPeek: {
          const std::int64_t address = integerArgument(context, argument(argc, argv, 0), "address");
          if (address < 0 || address > 0xFFFF) throw ScriptArgumentError("参数 address 必须在 0 到 65535 之间");
          const std::optional<std::uint8_t> value = host.cpuPeek(static_cast<std::uint16_t>(address));
          return value ? JS_NewInt32(context, *value) : JS_UNDEFINED;
        }
        case StorageRead:
          return JS_NewInt32(context, host.storageRead(index, integerArgument(context, argument(argc, argv, 0), "offset")));
        case StorageWrite: {
          const std::int64_t value = integerArgument(context, argument(argc, argv, 1), "value");
          if (value < 0 || value > 255) throw ScriptArgumentError("参数 value 必须在 0 到 255 之间");
          return JS_NewBool(context, host.storageWrite(index, integerArgument(context, argument(argc, argv, 0), "offset"), static_cast<std::uint8_t>(value)));
        }
        case StateGet:
          return JS_NewUint32(context, host.stateGet(index));
        case StateSet: {
          const std::int64_t value = integerArgument(context, argument(argc, argv, 0), "value");
          host.stateSet(index, static_cast<std::uint32_t>(static_cast<std::uint64_t>(value) & 0xFFFFFFFFU));
          return JS_UNDEFINED;
        }
        case IrqAssert:
          host.irqAssert();
          return JS_UNDEFINED;
        case IrqRelease:
          host.irqRelease();
          return JS_UNDEFINED;
        case PadSet: {
          std::string button;
          if (!stringArgument(context, argument(argc, argv, 0), button)) throw ScriptArgumentError("参数 button 必须是字符串");
          const JSValue down = argument(argc, argv, 1);
          if (!JS_IsBool(down)) throw ScriptArgumentError("参数 down 必须是布尔值");
          host.padSet(index, button, JS_ToBool(context, down) != 0);
          return JS_UNDEFINED;
        }
        case PadRelease:
          host.padRelease(index);
          return JS_UNDEFINED;
        case TapeLevel:
          return JS_NewInt32(context, host.tapeLevel() ? 1 : 0);
        case TapeSetComparator: {
          const std::int64_t low = integerArgument(context, argument(argc, argv, 0), "low");
          const std::int64_t high = integerArgument(context, argument(argc, argv, 1), "high");
          if (low < -32768 || high > 32767 || low >= high) throw ScriptArgumentError("参数 low 和 high 必须在 -32768 到 32767 之间，且 low 小于 high");
          const JSValue invert = argument(argc, argv, 2);
          if (!JS_IsBool(invert)) throw ScriptArgumentError("参数 invert 必须是布尔值");
          host.tapeSetComparator(static_cast<std::int16_t>(low), static_cast<std::int16_t>(high), JS_ToBool(context, invert) != 0);
          return JS_UNDEFINED;
        }
        case Cycle:
          return JS_NewInt64(context, static_cast<std::int64_t>(host.cycle()));
        case TimerAfter: {
          std::string name;
          if (!stringArgument(context, argument(argc, argv, 0), name)) throw ScriptArgumentError("参数 name 必须是字符串");
          const std::int64_t cycles = integerArgument(context, argument(argc, argv, 1), "cycles");
          if (cycles < 0 || cycles > MAX_TIMER_CYCLES) throw ScriptArgumentError("参数 cycles 必须在 0 到 1099511627776 之间");
          host.timerAfter(name, cycles);
          return JS_UNDEFINED;
        }
        case TimerCancel: {
          std::string name;
          if (!stringArgument(context, argument(argc, argv, 0), name)) throw ScriptArgumentError("参数 name 必须是字符串");
          host.timerCancel(name);
          return JS_UNDEFINED;
        }
        case CircuitWrite: {
          const std::int64_t address = integerArgument(context, argument(argc, argv, 0), "address");
          const std::int64_t value = integerArgument(context, argument(argc, argv, 1), "value");
          if (address < 0 || address > 0xFFFF) throw ScriptArgumentError("参数 address 必须在 0 到 65535 之间");
          if (value < 0 || value > 255) throw ScriptArgumentError("参数 value 必须在 0 到 255 之间");
          host.circuitWrite(index, static_cast<std::uint16_t>(address), static_cast<std::uint8_t>(value));
          return JS_UNDEFINED;
        }
        case CircuitGet: {
          std::string name;
          if (!stringArgument(context, argument(argc, argv, 0), name)) throw ScriptArgumentError("参数 name 必须是字符串");
          return JS_NewUint32(context, host.circuitGet(index, name));
        }
        case CircuitBank: {
          std::string space;
          if (!stringArgument(context, argument(argc, argv, 0), space) || (space != "program" && space != "character")) throw ScriptArgumentError("参数 space 必须是 'program' 或 'character'");
          const std::int64_t address = integerArgument(context, argument(argc, argv, 1), "address");
          if (address < 0 || address > 0xFFFF) throw ScriptArgumentError("参数 address 必须在 0 到 65535 之间");
          return JS_NewUint32(context, host.circuitBank(index, space == "character", static_cast<std::uint16_t>(address)));
        }
        case NametableMap: {
          const std::int64_t slot = integerArgument(context, argument(argc, argv, 0), "slot");
          if (slot < 0 || slot > 3) throw ScriptArgumentError("参数 slot 必须在 0 到 3 之间");
          std::string source;
          if (!stringArgument(context, argument(argc, argv, 1), source)) throw ScriptArgumentError("参数 source 必须是字符串");
          const std::int64_t bank = argc > 2 ? integerArgument(context, argv[2], "bank") : 0;
          host.nametableMap(static_cast<std::size_t>(slot), source, bank);
          return JS_UNDEFINED;
        }
        default:
          throw std::logic_error("未知的宿主函数");
      }
    });
  }

  /** ctx.window(id) 与 ctx.storage(id) 共用：按标识找对象。 */
  static JSValue lookup(JSContext* context, int argc, JSValueConst* argv, const std::vector<std::string>& names, const std::vector<JSValue>& objects, const char* kind) {
    std::string id;
    if (!stringArgument(context, argument(argc, argv, 0), id)) return JS_ThrowTypeError(context, "%s 的标识必须是字符串", kind);
    for (std::size_t index = 0; index < names.size(); ++index) {
      if (names[index] == id) return JS_DupValue(context, objects[index]);
    }
    return JS_ThrowRangeError(context, "%s不存在：%s", kind, id.c_str());
  }

  static JSValue lookupWindow(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
    const Impl* runtimeSelf = self(context);
    return lookup(context, argc, argv, runtimeSelf->layout.windows, runtimeSelf->windowObjects, "窗口");
  }

  static JSValue lookupStorage(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
    const Impl* runtimeSelf = self(context);
    return lookup(context, argc, argv, runtimeSelf->layout.storages, runtimeSelf->storageObjects, "存储区");
  }

  /** ctx.pad(port)：按口号找手柄口对象。 */
  static JSValue lookupPad(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
    const Impl* runtimeSelf = self(context);
    return guarded(context, [&]() -> JSValue {
      const std::int64_t port = integerArgument(context, argument(argc, argv, 0), "port");
      if (port < 0 || static_cast<std::uint64_t>(port) >= runtimeSelf->padObjects.size()) throw ScriptArgumentError("手柄口不存在：" + std::to_string(port));
      return JS_DupValue(context, runtimeSelf->padObjects[static_cast<std::size_t>(port)]);
    });
  }

  /** ctx.settings.get(key)：设置项的当前值；没有这个键时抛 RangeError。 */
  static JSValue settingsGet(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
    const Impl* runtimeSelf = self(context);
    std::string key;
    if (!stringArgument(context, argument(argc, argv, 0), key)) return JS_ThrowTypeError(context, "设置项的键必须是字符串");
    const JSValue value = JS_GetPropertyStr(context, runtimeSelf->settingValues, key.c_str());
    if (JS_IsUndefined(value)) return JS_ThrowRangeError(context, "设置项不存在：%s", key.c_str());
    return value;
  }

  /** ctx.circuit(id)：按标识找电路对象。 */
  static JSValue lookupCircuit(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
    const Impl* runtimeSelf = self(context);
    return lookup(context, argc, argv, runtimeSelf->layout.circuits, runtimeSelf->circuitObjects, "电路");
  }

  /** ctx.device(name)：取主机上的功能模块；目前只有 'tape'（磁带机），主机没有它时抓错。 */
  static JSValue lookupDevice(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
    const Impl* runtimeSelf = self(context);
    std::string name;
    if (!stringArgument(context, argument(argc, argv, 0), name)) return JS_ThrowTypeError(context, "功能模块的名称必须是字符串");
    if (name != "tape") return JS_ThrowRangeError(context, "没有这种功能模块：%s", name.c_str());
    if (!runtimeSelf->layout.hasTape) return JS_ThrowRangeError(context, "当前主机没有磁带机");
    return JS_DupValue(context, runtimeSelf->tapeObject);
  }

  /** ctx.keys.isDown(code)：这个物理键现在是否按着。 */
  static JSValue keysIsDown(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
    Impl* runtimeSelf = self(context);
    std::string code;
    if (!stringArgument(context, argument(argc, argv, 0), code)) return JS_ThrowTypeError(context, "键位必须是字符串");
    return guarded(context, [&]() -> JSValue { return JS_NewBool(context, runtimeSelf->bindings.keyDown(code)); });
  }

  static JSValue nametableSet(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
    Impl* runtimeSelf = self(context);
    std::string mode;
    if (!stringArgument(context, argument(argc, argv, 0), mode)) return JS_ThrowTypeError(context, "名称表连法必须是字符串");
    return guarded(context, [&]() -> JSValue {
      runtimeSelf->bindings.nametableSet(mode);
      return JS_UNDEFINED;
    });
  }

  static JSValue logWarn(JSContext* context, JSValueConst, int argc, JSValueConst* argv) {
    Impl* runtimeSelf = self(context);
    std::string message;
    if (!stringArgument(context, argument(argc, argv, 0), message)) return JS_ThrowTypeError(context, "日志内容必须是字符串");
    return guarded(context, [&]() -> JSValue {
      runtimeSelf->bindings.log(message);
      return JS_UNDEFINED;
    });
  }
};

ScriptRuntime::ScriptRuntime(std::string pluginId, std::string_view source, ScriptLayout layout, ScriptBindings& bindings, ScriptLimits limits)
    : impl_(std::make_unique<Impl>(std::move(pluginId), std::move(layout), bindings, limits)) {
  impl_->initialize(source);
}

ScriptRuntime::~ScriptRuntime() = default;

bool ScriptRuntime::hasReset() const noexcept { return !JS_IsUndefined(impl_->resetFn); }

bool ScriptRuntime::hasRead() const noexcept { return !JS_IsUndefined(impl_->readFn); }

bool ScriptRuntime::hasWrite() const noexcept { return !JS_IsUndefined(impl_->writeFn); }

bool ScriptRuntime::hasKey() const noexcept { return !JS_IsUndefined(impl_->keyFn); }

bool ScriptRuntime::hasMouse() const noexcept { return !JS_IsUndefined(impl_->mouseFn); }

bool ScriptRuntime::hasTapeEdge() const noexcept { return !JS_IsUndefined(impl_->tapeEdgeFn); }

bool ScriptRuntime::hasTimer() const noexcept { return !JS_IsUndefined(impl_->timerFn); }

bool ScriptRuntime::hasCircuit() const noexcept { return !JS_IsUndefined(impl_->circuitFn); }
bool ScriptRuntime::hasA12() const noexcept { return !JS_IsUndefined(impl_->a12Fn); }

bool ScriptRuntime::hasTable(std::string_view id) const {
  if (!JS_IsObject(impl_->tablesObject)) return false;
  const std::string name(id);
  const JSValue function = JS_GetPropertyStr(impl_->js, impl_->tablesObject, name.c_str());
  const bool found = JS_IsFunction(impl_->js, function);
  JS_FreeValue(impl_->js, function);
  return found;
}

std::uint32_t ScriptRuntime::table(std::string_view id, std::span<const std::uint32_t> values) {
  const std::string name(id);
  const std::string phase = "tables." + name;
  JSContext* js = impl_->js;
  const JSValue function = JS_IsObject(impl_->tablesObject) ? JS_GetPropertyStr(js, impl_->tablesObject, name.c_str()) : JS_UNDEFINED;
  if (!JS_IsFunction(js, function)) {
    JS_FreeValue(js, function);
    throw shared::CoreError(mapper_error::ERROR_SCRIPT_ERROR, "插件 " + impl_->pluginId + " 没有导出 tables." + name);
  }
  std::vector<JSValue> arguments;
  arguments.reserve(values.size() + 1);
  arguments.push_back(impl_->hostObject);
  for (const std::uint32_t value : values) arguments.push_back(JS_NewUint32(js, value));
  JS_UpdateStackTop(impl_->runtime);
  impl_->armDeadline();
  const JSValue result = JS_Call(js, function, impl_->tablesObject, static_cast<int>(arguments.size()), arguments.data());
  JS_FreeValue(js, function);
  if (JS_IsException(result)) throw impl_->describeFailure(phase.c_str(), false);
  double number = 0;
  const bool isNumber = JS_IsNumber(result) && JS_ToFloat64(js, &number, result) == 0;
  JS_FreeValue(js, result);
  if (!isNumber || number < 0 || number > 4294967295.0 || std::trunc(number) != number) {
    throw shared::CoreError(mapper_error::ERROR_SCRIPT_ERROR, "插件 " + impl_->pluginId + " 的 tables." + name + " 必须返回 0 到 4294967295 的整数");
  }
  return static_cast<std::uint32_t>(number);
}

void ScriptRuntime::tapeEdge(bool level, std::uint32_t samples) {
  if (!hasTapeEdge()) return;
  JSValue arguments[3] = {impl_->hostObject, JS_NewInt32(impl_->js, level ? 1 : 0), JS_NewUint32(impl_->js, samples)};
  JS_FreeValue(impl_->js, impl_->invoke("onTapeEdge", impl_->tapeEdgeFn, 3, arguments));
}

void ScriptRuntime::timer(std::string_view name) {
  if (!hasTimer()) return;
  impl_->invokeInput("onTimer", impl_->timerFn, JS_NewStringLen(impl_->js, name.data(), name.size()));
}

void ScriptRuntime::a12() {
  if (!hasA12()) return;
  JSValue arguments[1] = {impl_->hostObject};
  JS_FreeValue(impl_->js, impl_->invoke("onA12", impl_->a12Fn, 1, arguments));
}

void ScriptRuntime::circuit(std::string_view id) {
  if (!hasCircuit()) return;
  impl_->invokeInput("onCircuit", impl_->circuitFn, JS_NewStringLen(impl_->js, id.data(), id.size()));
}

void ScriptRuntime::key(const contracts::hardware::KeyInput& input) {
  if (hasKey()) impl_->invokeInput("onKey", impl_->keyFn, impl_->keyEvent(input));
}

void ScriptRuntime::mouse(const contracts::hardware::MouseInput& input) {
  if (hasMouse()) impl_->invokeInput("onMouse", impl_->mouseFn, impl_->mouseEvent(input));
}

void ScriptRuntime::reset(bool power) {
  if (!hasReset()) return;
  JSValue arguments[2] = {impl_->hostObject, power ? impl_->powerName : impl_->softName};
  JS_FreeValue(impl_->js, impl_->invoke("reset", impl_->resetFn, 2, arguments));
}

std::optional<std::uint8_t> ScriptRuntime::read(bool ppu, std::uint16_t address) {
  if (!hasRead()) return std::nullopt;
  JSValue arguments[3] = {impl_->hostObject, ppu ? impl_->ppuName : impl_->cpuName, JS_NewInt32(impl_->js, address)};
  const JSValue result = impl_->invoke("read", impl_->readFn, 3, arguments);
  if (JS_IsUndefined(result)) return std::nullopt;
  double number = 0;
  const bool isNumber = JS_IsNumber(result) && JS_ToFloat64(impl_->js, &number, result) == 0;
  JS_FreeValue(impl_->js, result);
  if (!isNumber || number < 0 || number > 255 || std::trunc(number) != number) {
    throw shared::CoreError(mapper_error::ERROR_SCRIPT_ERROR, "插件 " + impl_->pluginId + " 的 read 必须返回 0 到 255 的整数或 undefined");
  }
  return static_cast<std::uint8_t>(number);
}

void ScriptRuntime::write(bool ppu, std::uint16_t address, std::uint8_t value) {
  if (!hasWrite()) return;
  JSValue arguments[4] = {impl_->hostObject, ppu ? impl_->ppuName : impl_->cpuName, JS_NewInt32(impl_->js, address), JS_NewInt32(impl_->js, value)};
  JS_FreeValue(impl_->js, impl_->invoke("write", impl_->writeFn, 4, arguments));
}

}  // namespace ember::infra::mappers
