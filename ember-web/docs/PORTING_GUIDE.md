# Ember Web 移植指南

本文档详细说明如何将 Ember FC 模拟器从桌面版移植到 WebAssembly 版。

## 一、架构差异

### 桌面版架构

```
┌─────────────────────────────────────────────┐
│              Desktop App (Qt/Electron)       │
├─────────────────────────────────────────────┤
│              Core (core.hpp)                 │
│  ┌─────────────┐  ┌──────────────────────┐  │
│  │ RequestRouter│  │ ThreadPool           │  │
│  │ (JSON RPC)   │  │ (模拟线程)           │  │
│  └─────────────┘  └──────────────────────┘  │
│         │                  │                 │
│         ▼                  ▼                 │
│  ┌──────────────────────────────────┐       │
│  │     Runtime Service              │       │
│  │  (运行服务、状态管理、事件)       │       │
│  └──────────────────────────────────┘       │
│              │                              │
│              ▼                              │
│  ┌──────────────────────────────────┐       │
│  │     Machine (FcMachine)          │       │
│  │  CPU / PPU / APU / Cartridge     │       │
│  └──────────────────────────────────┘       │
└─────────────────────────────────────────────┘
```

### Web 版架构

```
┌─────────────────────────────────────────────┐
│              React Frontend                  │
│  (Canvas + Web Audio + Keyboard)             │
├─────────────────────────────────────────────┤
│              Embind (JS ↔ C++)               │
├─────────────────────────────────────────────┤
│         Web C API (ember_web_api)            │
│  (直接封装 Machine 层，无 Core/线程池)       │
├─────────────────────────────────────────────┤
│  ┌──────────────────────────────────┐       │
│  │     Machine (FcMachine)          │       │
│  │  CPU / PPU / APU / Cartridge     │       │
│  └──────────────────────────────────┘       │
│         (WASM 单线程运行)                    │
└─────────────────────────────────────────────┘
```

### 为什么跳过 Core 层？

1. **单线程环境**：WebAssembly 是单线程的（SharedArrayBuffer 有兼容性问题），Core 层的线程池没有意义
2. **简化接口**：直接调用 `runFrame()` 比通过 JSON RPC 请求简单得多
3. **性能**：减少一层抽象，降低 JS ↔ WASM 调用开销
4. **帧步进**：浏览器用 `requestAnimationFrame` 驱动，正好对应 `runFrame()`

## 二、移植步骤

### 步骤 1：梳理核心源码

从原始项目中提取必需的源码文件：

```
core/src/
├── bootstrap/
│   ├── machines/fc/          # FC 机器装配（必需）
│   ├── machines/common/      # 公共组件（mapper-circuit-catalog 等）
│   ├── modules/              # 模块登记（mappers, cartridges 等）
│   └── core.hpp/cpp          # Core 入口（Web 版不需要）
├── contracts/                # 所有接口契约（必需）
├── logic/
│   ├── hardware/             # 硬件实现：CPU, PPU, APU, bus, scheduler...
│   ├── cartridges/           # iNES 解析、卡带装载
│   └── machines/             # 机器管理（Web 版不需要）
├── infra/
│   ├── mappers/              # mapper 插件系统（需要 QuickJS）
│   └── machines/             # ROM 存储（Web 版不需要）
├── platform/
│   ├── audio/                # 音频抽象（Web 用 Web Audio API 替代）
│   ├── events/               # 事件系统
│   ├── logging/              # 错误日志
│   ├── streams/              # 帧输出
│   ├── time/                 # 时钟
│   ├── files/                # 文件系统
│   ├── network/              # 网络（Web 版排除）
│   ├── requests/             # 请求路由（Web 版排除）
│   └── threading/            # 线程（Web 版排除）
└── shared/                   # 共享工具：错误、JSON、音频、哈希
```

### 步骤 2：创建 Web 平台适配层

在 `core/src/platform/web/` 下创建三个文件：

#### 2.1 ember_web_api.hpp

定义 C 语言接口，供 Embind 绑定。设计原则：
- 所有函数用 C 链接（`extern "C"`）
- 参数用基本类型（指针、整数、浮点）
- 不抛出异常，用返回码 + lastError 传递错误
- 模拟器用不透明指针 `EmberEmulator*` 封装

#### 2.2 ember_web_api.cpp

实现 C API，内部使用 C++ 对象：

```cpp
struct EmberEmulator {
  unique_ptr<FcMachine> machine;
  shared_ptr<PluginRegistry> plugins;
  unique_ptr<CartridgeLoader> loader;
  unique_ptr<MapperCartridge> cartridge;
  vector<float> audioBuffer;
  string lastError;
  bool paused;
  uint64_t frameCount;
};
```

关键实现要点：

**创建实例**：
```cpp
EmberEmulator* ember_create() {
  auto* emu = new EmberEmulator();
  emu->machine = make_unique<FcMachine>();
  // 创建 PluginRegistry（mapper 工厂）
  auto options = PluginRegistryOptions{...};
  emu->plugins = make_shared<PluginRegistry>(move(options));
  // 创建 iNES 装载器
  emu->loader = make_unique<InesCartridgeLoader>(emu->plugins);
  return emu;
}
```

**加载 ROM**：
```cpp
int ember_load_rom(EmberEmulator* emu, const uint8_t* data, size_t size) {
  // 1. 解析 iNES → CartridgePlan
  auto plan = emu->loader->plan(span(data, size));
  // 2. 装配卡带（需要主机的 IRQ 和 PadSink）
  emu->cartridge = emu->loader->assemble(
      plan, emu->machine->cartridgeIrq(), *emu->machine);
  // 3. 插入卡槽
  emu->machine->cartridgeSlot().insert(*emu->cartridge);
}
```

**运行一帧**：
```cpp
uint64_t ember_run_frame(EmberEmulator* emu) {
  emu->machine->runFrame();
  return ++emu->frameCount;
}
```

**获取帧缓冲**：
```cpp
const uint8_t* ember_get_framebuffer(EmberEmulator* emu) {
  auto fb = emu->machine->frame();
  // FrameBuffer.pixels 是 span<const uint32_t>
  // 像素格式：每像素 4 字节，R、G、B、A 顺序
  // 可直接用于 WebGL/Canvas 的 RGBA8 纹理
  return reinterpret_cast<const uint8_t*>(fb.pixels.data());
}
```

**音频采样**：
```cpp
int ember_get_audio_samples(EmberEmulator* emu, float* out, int max) {
  emu->audioBuffer.clear();
  emu->machine->takeAudio(emu->audioBuffer);
  int count = min(audioBuffer.size(), (size_t)max);
  memcpy(out, audioBuffer.data(), count * sizeof(float));
  return count;
}
```

#### 2.3 wasm_bindings.cpp

使用 Emscripten 的 Embind 将 C++ 类暴露给 JavaScript：

```cpp
#include <emscripten/bind.h>

class EmberWasm {
 public:
  EmberWasm() : emu_(ember_create()) {}
  ~EmberWasm() { ember_destroy(emu_); }

  int loadRom(const val& data) { ... }
  void powerOn() { ember_power_on(emu_); }
  // ... 其他方法

 private:
  EmberEmulator* emu_;
};

EMSCRIPTEN_BINDINGS(ember_wasm) {
  class_<EmberWasm>("EmberWasm")
    .constructor<>()
    .function("loadRom", &EmberWasm::loadRom)
    .function("powerOn", &EmberWasm::powerOn)
    // ...
    ;
}
```

### 步骤 3：配置 CMakeLists.txt

基于原始 CMakeLists.txt 改编，关键改动：

1. **目标类型**：从 `STATIC` 库改为 `add_executable`（Emscripten 生成 .js + .wasm）
2. **排除不需要的模块**：
   - `platform/network/` - 网络
   - `platform/requests/` - 请求路由
   - `platform/threading/` - 线程
   - `*-win.cpp` / `*-mac.cpp` / `*-linux.cpp` - 平台特定
3. **添加 Web 平台文件**：`platform/web/*.cpp`
4. **移除不需要的依赖**：
   - `ember_miniaudio` - 改用 Web Audio API
   - `ember_ffmpeg` - 磁带机解码（Web 版暂不支持）
5. **Emscripten 链接选项**：
   - `--bind` - 启用 Embind
   - `-s MODULARIZE=1` - 模块化输出
   - `-s ALLOW_MEMORY_GROWTH=1` - 允许内存增长
   - `-s FILESYSTEM=1` - 启用虚拟文件系统（mapper 插件需要）
   - `--preload-file plugins@/plugins` - 预加载 mapper 插件

### 步骤 4：前端实现

#### 4.1 useEmulator Hook

核心 React Hook，管理：
- WASM 模块加载
- 模拟器实例生命周期
- 主循环（requestAnimationFrame）
- 音频（ScriptProcessorNode）
- 键盘输入

#### 4.2 画面渲染

使用 Canvas 2D API：
```javascript
// 每帧从 WASM 内存读取像素并上传
const ptr = instance.getFramebufferPtr();
const data = new Uint8ClampedArray(Module.HEAPU8.buffer, ptr, w * h * 4);
const imageData = new ImageData(data, w, h);
ctx.putImageData(imageData, 0, 0);
```

性能优化：
- 关闭 `imageSmoothingEnabled` 保持像素风格
- 用 `image-rendering: pixelated` CSS 属性
- 预分配 ImageData，只更新 data

#### 4.3 音频输出

使用 Web Audio API 的 `ScriptProcessorNode`：
```javascript
const processor = ctx.createScriptProcessor(2048, 0, 1);
processor.onaudioprocess = (e) => {
  const output = e.outputBuffer.getChannelData(0);
  const samplesRead = instance.getAudioSamples(output);
  // 不足部分填 0
};
```

注意事项：
- `AudioContext` 必须在用户交互后才能启动（浏览器自动播放策略）
- 采样率与 APU 原生采样率一致（约 223722 Hz？不对，需要确认）
- 用 GainNode 控制音量

#### 4.4 手柄输入

两种输入方式：
1. **键盘**：监听 `keydown` / `keyup`，维护按键位掩码
2. **触屏**：虚拟按钮，`pointerdown` / `pointerup` 事件

按键映射（位掩码）：
```
A      = 0x01
B      = 0x02
SELECT = 0x04
START  = 0x08
UP     = 0x10
DOWN   = 0x20
LEFT   = 0x40
RIGHT  = 0x80
```

### 步骤 5：Mapper 插件处理

Ember 的 mapper 系统基于 QuickJS + JS 插件。Web 版有两种方案：

#### 方案 A：编译 QuickJS 到 WASM（推荐）

将 QuickJS-ng 随核心一起编译为 WASM，插件保持不变。

优点：
- 与桌面版行为完全一致
- 所有 50+ mapper 直接可用
- 插件包格式不变

缺点：
- 包体积较大（QuickJS 约 200KB）
- 需要预加载插件文件

实现：
```cmake
# CMakeLists.txt
add_library(ember_quickjs STATIC
  third_party/quickjs-ng/quickjs.c
  third_party/quickjs-ng/libregexp.c
  third_party/quickjs-ng/libunicode.c
  third_party/quickjs-ng/dtoa.c)

# 预加载插件目录
set_target_properties(ember_wasm PROPERTIES
  LINK_FLAGS "--preload-file ${CMAKE_SOURCE_DIR}/../plugins@/plugins"
)
```

#### 方案 B：使用浏览器 JS 引擎（高级）

将 mapper 插件的 JS 直接运行在浏览器中，通过 Embind 与 C++ 交互。

优点：
- 性能更好（原生 JS 引擎）
- 包体积更小（不需要 QuickJS）

缺点：
- 需要重写 ScriptRuntime 的绑定层
- 沙箱安全性降低（插件运行在主线程）
- 工作量大

## 三、常见问题

### Q: 编译时找不到 generated/ 目录下的文件？

A: Web 版不需要 schema 生成的文件（它们是 Core 层的 JSON 请求/响应类型）。CMakeLists.txt 已经排除了 `generated/` 目录。如果某些源文件依赖它们，需要：
1. 从原始项目的 `schema/` 目录生成
2. 或者修改源码移除对这些类型的依赖

### Q: miniaudio 相关的编译错误？

A: Web 版使用 Web Audio API 替代 miniaudio。需要确保：
1. `platform/audio/` 下没有直接引用 miniaudio 的代码
2. 或者排除这些文件，用 Web 专用实现替代

### Q: 音频采样率是多少？

A: 2A03 APU 的原生采样率由 `Apu2a03::SAMPLE_RATE` 常量定义。Web Audio API 会自动重采样到音频设备的采样率。

### Q: 每帧多少 CPU 周期？

A: NTSC 制式：
- 每帧 262 扫描线
- 每扫描线 341 个 PPU 点（奇数帧少 1 个点）
- PPU 点 = 4 个主时钟周期
- CPU 周期 = 12 个主时钟周期
- 所以每帧约 (341 × 262 - 0.5) × 4 / 12 ≈ 29780.5 CPU 周期

### Q: 帧缓冲的像素格式？

A: 每像素 4 字节，内存顺序为 R、G、B、A（A 恒为 255）。小端机器上 `uint32_t` 值为 `0xAABBGGRR`。可以直接上传为 WebGL 的 `RGBA` 格式纹理，或用于 Canvas 的 `ImageData`。

## 四、后续优化方向

1. **WebGL 渲染**：用 WebGL 替代 Canvas 2D，性能更好
2. **AudioWorklet**：用 AudioWorklet 替代 ScriptProcessorNode（后者已废弃）
3. **SIMD 优化**：启用 WASM SIMD 提升 PPU 渲染性能
4. **多线程**：使用 SharedArrayBuffer + Web Worker 实现音频线程分离
5. **即时存档**：支持游戏进度保存/加载
6. **金手指**：支持作弊码
7. **Netplay**：网络对战
8. **调试器**：反汇编、内存查看、断点

## 五、文件清单

Web 移植新增/修改的文件：

```
新增：
├── core/src/platform/web/ember_web_api.hpp    # C API 头
├── core/src/platform/web/ember_web_api.cpp    # C API 实现
├── core/src/platform/web/wasm_bindings.cpp    # Embind 绑定
├── core/CMakeLists.txt                        # Emscripten 构建配置
├── web/                                       # React 前端（全部新增）
├── scripts/build-wasm.sh                      # WASM 构建脚本
├── scripts/build-all.sh                       # 完整构建脚本
└── docs/                                      # 文档（新增）

修改：
（核心源码不需要修改，Web 适配层完全独立）
```
