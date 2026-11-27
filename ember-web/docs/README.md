# Ember Web - WebAssembly FC/NES 模拟器

基于 [Ember](https://github.com/sungbindev/famiclone) 模拟器核心的网页版移植，使用 Emscripten 将 C++ 核心编译为 WebAssembly，前端采用 React + Vite。

## 功能特性

- 🎮 **完整 FC/NES 模拟**：6502 CPU、2C02 PPU、2A03 APU
- 🕹️ **手柄支持**：键盘 + 触屏虚拟手柄，支持双人
- 🔊 **音频输出**：Web Audio API，原生采样率播放
- 📦 **iNES 格式**：支持 .nes ROM 文件加载
- 🧩 **Mapper 插件系统**：基于 QuickJS 的 50+ 种 mapper 支持
- ⚡ **WebAssembly 高性能**：接近原生速度
- 📱 **响应式设计**：支持桌面和移动设备

## 项目结构

```
ember-web/
├── core/                          # C++ 核心（Ember Core + Web 适配层）
│   ├── src/
│   │   ├── bootstrap/             # 机器装配（FC 等）
│   │   ├── contracts/             # 接口契约
│   │   ├── logic/                 # 硬件逻辑实现（CPU/PPU/APU 等）
│   │   ├── infra/                 # 基础设施（mappers, cartridges 等）
│   │   ├── platform/
│   │   │   └── web/               # Web 平台适配层
│   │   │       ├── ember_web_api.hpp   # C API 头文件
│   │   │       ├── ember_web_api.cpp   # C API 实现
│   │   │       └── wasm_bindings.cpp   # Embind 绑定
│   │   └── shared/                # 共享工具
│   ├── third_party/               # 第三方库
│   │   ├── nlohmann-json/         # JSON 库
│   │   ├── miniz.c/.h             # 压缩库
│   │   └── quickjs-ng/            # QuickJS（mapper 插件，可选）
│   ├── CMakeLists.txt             # Emscripten 构建配置
│   └── CMakeLists.txt.orig        # 原始构建配置（参考）
├── web/                           # React 前端
│   ├── src/
│   │   ├── components/            # UI 组件
│   │   ├── hooks/                 # React Hooks
│   │   ├── types/                 # TypeScript 类型
│   │   ├── styles/                # 样式
│   │   ├── ember/                 # WASM 输出（构建后生成）
│   │   ├── App.tsx                # 主应用
│   │   └── main.tsx               # 入口
│   ├── index.html
│   ├── package.json
│   ├── vite.config.ts
│   └── tsconfig.json
├── plugins/                       # Mapper 插件（.emap 包）
├── scripts/
│   ├── build-wasm.sh              # WASM 构建脚本
│   └── build-all.sh               # 完整构建脚本
└── docs/
    ├── README.md                  # 本文件
    └── PORTING_GUIDE.md           # 移植指南
```

## 快速开始

### 前置要求

- [Emscripten SDK](https://emscripten.org/docs/getting_started/downloads.html) (3.1.50+)
- Node.js (18+)
- CMake (3.25+)

### 构建步骤

#### 1. 准备源码

将完整的 Ember Core 源码放入 `core/` 目录，确保包含：
- `src/` 全部源码
- `third_party/` 第三方库
  - `nlohmann-json/`（必需）
  - `miniz.c / miniz.h`（必需）
  - `quickjs-ng/`（可选，mapper 插件支持）

#### 2. 构建 WASM

```bash
# 激活 Emscripten 环境
source /path/to/emsdk/emsdk_env.sh

# 构建
./scripts/build-wasm.sh Release
```

构建产物将自动复制到 `web/src/ember/` 目录。

#### 3. 运行前端

```bash
cd web
npm install
npm run dev
```

浏览器打开 http://localhost:5173 即可使用。

#### 4. 生产构建

```bash
./scripts/build-all.sh
```

产物在 `web/dist/` 目录，可部署到任意静态文件服务器。

## 使用说明

### 加载 ROM

1. 点击「加载 ROM」按钮，或直接拖拽 `.nes` 文件到页面
2. 点击「开机」按钮启动模拟器

### 键盘操作

**玩家 1：**
- 方向键：上下左右
- Z：A 键
- X：B 键
- Shift：SELECT
- Enter：START

**玩家 2：**
- W/A/S/D：上下左右
- J：A 键
- K：B 键
- U：SELECT
- I：START

### 触屏操作

勾选「显示触屏手柄」即可使用虚拟按键。

## Mapper 支持

Ember 使用基于 QuickJS 的插件式 mapper 系统。要启用完整 mapper 支持：

1. 确保 `core/third_party/quickjs-ng/` 包含 QuickJS 源码
2. 将 mapper 插件（.emap 包）放入 `plugins/` 目录
3. 重新构建 WASM

构建时 `--preload-file` 会将 `plugins/` 目录打包进 WASM 的虚拟文件系统。

### 内置支持

如果没有 QuickJS，模拟器仍可运行，但只支持：
- **Mapper 0 (NROM)**：最简单的 mapper，无 bank switching

## 架构说明

### 移植策略

Web 版采用「直接 Machine 层」策略，绕过了桌面版的 Core/RequestRouter/ThreadPool 多线程架构：

```
JavaScript (React)
    ↓ Embind
  C API (ember_web_api)
    ↓
  FcMachine (Machine 层)
    ├─ 6502 CPU
    ├─ 2C02 PPU
    ├─ 2A03 APU
    └─ Cartridge Slot
         ↔ Mapper Plugins (QuickJS)
```

原因：
1. Web 是单线程环境，多线程架构增加复杂度且收益有限
2. 简化接口，减少 JS ↔ WASM 调用开销
3. 帧步进模式（runFrame）更适合浏览器的 requestAnimationFrame

### 关键文件

| 文件 | 说明 |
|------|------|
| `core/src/platform/web/ember_web_api.hpp` | C API 头文件，定义所有导出函数 |
| `core/src/platform/web/ember_web_api.cpp` | C API 实现，封装 FcMachine |
| `core/src/platform/web/wasm_bindings.cpp` | Embind 绑定，暴露给 JS |
| `core/CMakeLists.txt` | Emscripten 构建配置 |
| `web/src/hooks/useEmulator.ts` | React Hook，管理模拟器生命周期 |
| `web/src/components/EmulatorCanvas.tsx` | Canvas 画面渲染 |

## 性能优化建议

1. **使用 Release 构建**：`./scripts/build-wasm.sh Release`，启用 `-O2` 优化
2. **启用 SIMD**：如需更高性能，可在 CMakeLists.txt 中添加 `-msimd128`
3. **浏览器设置**：
   - Chrome/Edge 性能最佳
   - 确保浏览器硬件加速已开启
   - 关闭不必要的浏览器标签页

## 已知限制

- 不支持磁带机（Family BASIC 数据录音机）
- 不支持软盘驱动器
- 不支持打印机
- 不支持网络对战
- 调试功能（反汇编、断点等）暂未开放
- Safari 浏览器可能存在音频兼容性问题

## 故障排除

### 画面黑屏但有声音

检查 PPU 帧缓冲是否正确生成。打开浏览器开发者工具，查看 Console 是否有错误。

### 有画面没声音

1. 确保与页面有过交互（浏览器自动播放策略）
2. 检查音量是否为 0
3. 查看浏览器控制台是否有 AudioContext 相关错误

### ROM 加载失败

1. 确认是标准 iNES 格式（.nes 文件，以 "NES\x1A" 开头）
2. 检查 mapper 号是否受支持
3. 查看错误消息了解具体原因

### 性能不佳

1. 使用 Release 构建而非 Debug
2. 关闭浏览器开发者工具
3. 降低画面缩放比例
4. 检查是否启用了浏览器硬件加速

## 许可证

Ember Core 版权归原作者所有。Web 移植层代码按与原项目一致的许可证发布。

## 致谢

- [sungbindev/famiclone](https://github.com/sungbindev/famiclone) - Ember 模拟器原项目
- Emscripten 团队 - WebAssembly 编译工具链
- QuickJS 团队 - JavaScript 引擎
