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

**注意：`dist/` 不能直接双击 `index.html` 打开。** 浏览器在 `file://` 协议下不允许加载
`.wasm`，双击只会看到白屏。必须通过 HTTP 访问：

```bash
npx serve web/dist                 # 或
python3 -m http.server 8080 -d web/dist
```

服务器需保证 `.wasm` 以 `application/wasm` 返回、`.data` 以 `application/octet-stream`
返回。本项目为单线程编译，不需要 COOP/COEP 响应头。

如果不想自己编译，也可以直接到仓库的 Releases 页下载现成的
`ember-web-<版本>-dist.zip`，解压后用同样的方式通过 HTTP 打开即可，包内已含
编译好的 `.wasm` 与全部机型 ROM。

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

### 移动端界面

手机上的界面经过专门优化：

- **底部控制面板**：收起时只留一条把手贴在底栏上方，点它展开。展开高度按电视画面下方的空隙自适应 —— 空隙宽裕时只长到空隙里、不遮挡画面；空隙紧张（如横屏）才向上覆盖画面。
- **虚拟键盘**：顶栏「⌨ 键盘」按钮唤起系统输入法。这个按钮只在触屏 / 窄屏设备显示，桌面端隐藏。
- **屏幕旋转**：横竖屏切换时画面自动重新缩放，不会溢出屏幕。
- **桌面端**：布局不受任何影响，右侧栏常驻。

### 软盘

外设面板里的软驱支持导入、新建、写保护和保存。

**新建空白盘**：点「新建空白盘」直接插进驱动器，固定 1.44MB。

镜像内容是**全零**，尺寸 1474560 字节，格式由核心按字节数反推（上游 `formatFor`：
80 磁道 / 双面 / 每道 18 扇区 / 500 kbps）。

界面不提供容量选择 —— 学习机会按自己支持的格式重新格式化这张盘，选大小没有意义。
空白盘不含文件系统，格式化之后才能存文件。

**导入镜像**：支持 `.img` `.dsk` `.fdi` `.td0` `.86f`。

**写保护**：盘片插入后，那一行有个「写保护」勾选框。勾上后这张盘变只读，
学习机执行写数据、格式化命令时会收到「不可写」错误（FDC 的 `ST1_NOT_WRITEABLE`），
跟真机把磁盘角上的滑块拨到保护位置效果一致。每张盘单独设置，多驱可以分别控制。

**保存镜像**：学习机写盘后状态会显示「（已修改）」，点「保存」把当前镜像下载到本地。
浏览器写不了原文件，所以是下载一份新的，文件名沿用盘片名（空白盘就是 `blank-720k.img`）。

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

- 不支持网络对战
- 调试功能（反汇编、断点等）暂未开放
- Safari 浏览器可能存在音频兼容性问题
- 新建空白盘固定 1.44MB；导入其他尺寸的镜像时，格式由尺寸反推，认不出的按 1.44MB 推断

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

完整致谢见仓库根目录 [README.md](../../README.md#致谢)。核心几项：

- [sungbindev/famiclone](https://github.com/sungbindev/famiclone) — Ember 模拟器原项目（GPL-3.0）
- [em-fceux](https://github.com/tsone/fceux/tree/emscripten) — 3D CRT 电视机渲染管线来源，作者 Valtteri "tsone" Heikkilä（GPL-2.0-or-later）
- [FCEUX](https://github.com/TASEmulators/fceux) — em-fceux 的上游（GPL-2.0）
- Emscripten 团队 — WebAssembly 编译工具链
- QuickJS-ng 团队 — 插件脚本引擎

