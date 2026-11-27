# Ember Web

在浏览器里跑的 FC/NES 模拟器，支持裕兴、步步高、小霸王、金字塔等 6502 学习机。

基于 [Ember](https://github.com/sungbindev/famiclone) 模拟器核心移植：核心 C++ 代码用
Emscripten 编译成 WebAssembly，前端是 React + Vite，产物是纯静态文件，丢到任何静态服务器
就能用。

支持两种运行模式：

- **FC 卡带模式**：加载 iNES 格式的 `.nes` 卡带，通过 mapper 插件系统适配各种卡带电路
- **学习机模式**：裕兴、步步高、小霸王、金字塔、中华学习机等整机，ROM 相当于内置在主板上

## 目录结构

```
.
├── ember/              上游 Ember 模拟器源码（GPL-3.0，原样拷贝，见下方说明）
│   ├── core/           核心 C++ 源码 + 第三方库
│   ├── plugins/        234 个 mapper 插件（打包进 wasm 的虚拟文件系统）
│   ├── schema/         契约 schema
│   └── tools/          schema-gen 等构建脚本
├── ember-web/          Web 移植工程
│   ├── CMakeLists.txt  Web 版构建入口
│   ├── src/platform/web/   适配层（C API + Embind 绑定）
│   ├── scripts/        构建脚本
│   ├── docs/           移植说明
│   └── web/            React + Vite 前端
│       ├── src/        TypeScript 源码
│       └── public/     静态资源（ROM、wasm 产物）
└── LICENSE             GPL-3.0
```

## 关于 `ember/` 目录

`ember/` 是 [sungbindev/famiclone](https://github.com/sungbindev/famiclone) 仓库里
`ember/` 子目录的**原样拷贝**，版本 0.3.9，仅作为本项目的编译输入，未做任何修改。

之所以把它放进仓库而不是让使用者自己去拉，是为了让克隆下来就能直接构建 —— 版本锁定，
不会因为上游更新导致编译失败。上游更新后同步这个目录即可。

该目录使用 GPL-3.0-only 许可，原始版权归 sungbin 所有。

## 快速开始

项目提供两种使用方式：**直接用现成部署包**，或者**从源码自己编译**。

### 方式一：下载部署包（无需编译）

到 [Releases](../../releases) 页下载最新版的 `ember-web-<版本>-dist.zip`，解压后通过任意 HTTP
服务器打开即可。包内已含编译好的 `.wasm` 与全部机型 ROM，不需要 Emscripten、不需要 Node.js。

本地起个静态服务器最省事：

```bash
unzip ember-web-1.0.0-dist.zip -d ember-web
cd ember-web
npx serve .          # 或 python3 -m http.server 8080
```

### 方式二：从源码编译

需要 **Emscripten SDK**（3.1.62 验证过）和 **Node.js**（18 以上）。

```bash
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk && ./emsdk install latest && source ./emsdk_env.sh
```

然后编译 WebAssembly 模块。**这一步必须在编译前端之前完成**：

```bash
cd ember-web
bash scripts/build-wasm.sh
```

产物会复制到 `web/src/ember/`（胶水 JS）和 `web/public/wasm/`（wasm 与 data）。

最后编译前端：

```bash
cd web
npm install
npm run dev     # 开发模式 http://localhost:5173
npm run build   # 产出到 web/dist/，纯静态，可直接部署
```

### 部署注意事项

- **不能直接双击 `index.html` 打开。** 浏览器在 `file://` 协议下不允许加载 `.wasm`，
  双击只会得到一片白屏。必须通过 HTTP 访问，例如 `npx serve`、
  `python3 -m http.server` 或任意 Web 服务器。
- `.wasm` 必须以 `application/wasm` 返回，`.data` 以 `application/octet-stream` 返回。
  Nginx 若在 `server` 级定义了 `types` 块，会覆盖 MIME 表导致白屏，应写在 `location` 里。
- 项目为单线程编译，**不需要** COOP/COEP 响应头。
- `web/dist/` 是纯静态产物，可直接丢到任意子目录部署；资源引用为相对路径。

## 支持的机型

加载 ROM 时自动按 iNES mapper 号 + PRG 大小识别：

| 机型标识 | 名称 | mapper | PRG 大小 |
|---------|------|--------|----------|
| `fc` | 经典 FC/NES（卡带模式） | 其他 | — |
| `yuxing` | 裕兴学习机 | 169 | ≥ 1MB |
| `sb97` | 小霸王 97 | 168 | ≥ 1MB |
| `sb2000` | 小霸王 2000 | 171 | ≤ 512KB |
| `bbg1` | 步步高软驱一号 | 171 | ≤ 128KB |
| `bbg98` | 步步高 98 | 171 | > 512KB |
| `pec9588` | 金字塔 PEC-9588 | 179 | ≤ 512KB |
| `cec1` | 中华学习机 CEC-I | — | 64KB |

内置机型清单在 `ember-web/web/public/roms/machines.json`，新增机型不需要改代码，
详见同目录的 `如何新增机型.md`。

## 关于 ROM 文件

`ember-web/web/public/roms/` 目录里是各个学习机的 ROM 固件，仅用于运行模拟器。

**这些固件是有版权的第三方内容，版权归各自厂商所有。本项目不对其主张任何权利，
也不承担由这些文件引起的任何责任。** 如果你是版权持有者并希望移除，请提 issue。

上游 famiclone 仓库同样在 `reference/` 目录公开存放这些文件，本项目的做法与其一致。

## 外设支持

学习机的键盘、软驱、打印机、磁带机都可以在浏览器里直接使用：

- **键盘**：学习机模式下全键盘透传，按 `KeyboardEvent.code` 转发给模拟器；
  手机上通过隐藏的 textarea 挂系统输入法，支持字母、上档符号、方向键、回车退格
- **鼠标**：支持鼠标的机型自动启用画面捕获，坐标从屏幕映射到 256×240 帧坐标
- **软驱**：支持 `.img` / `.dsk` / `.fdi` / `.td0` / `.86f` 镜像，可写回下载
- **打印机**：捕获打印输出，1 位位图渲染
- **磁带机**：支持 WAV / MP3 / OGG / FLAC，自动重采样到 96kHz

## 致谢

这个项目建立在很多前人的工作上。以下按贡献大小排列：

### 模拟器核心

- **[sungbindev/famiclone](https://github.com/sungbindev/famiclone)** — Ember 模拟器原项目。
  本项目的核心（CPU、PPU、APU、mapper、学习机机型和全部外设）完全来自这里，
  作者 sungbin。没有它就没有这个 Web 版。**这是最需要感谢的一个项目。**
  许可 GPL-3.0-only。

### 渲染管线

- **[em-fceux](https://github.com/tsone/fceux/tree/emscripten)**（作者 Valtteri "tsone" Heikkilä）—
  3D CRT 电视机渲染管线移植自它的 `es2.cpp` / `es2util.cpp` / `meshes.h` 与着色器。
  包括 passScreen → passDownsample → passTV → passCombine 的 pass 结构、电视机网格数据
  （本项目的 `tvMesh.json`）、扫描线／会聚／辉光／荧光余辉的全部参数与换算公式。
  许可 GPL-2.0-or-later。
- **[FCEUX](https://github.com/TASEmulators/fceux)** — em-fceux 的上游，渲染思路与
  NTSC 复合信号模拟的来源。许可 GPL-2.0。

### 开发参考

以下项目没有直接使用其代码，但在实现过程中提供了关键的参考与验证基准：

- **[Mesen](https://github.com/SourMesen/Mesen2)** — 代码/数据标记（CDL）的位定义、
  跟踪日志格式参考。
- **[blargg 的测试 ROM](https://github.com/christopherpow/nes-test-roms)** — PPU 时序、
  APU 长度计数与帧计数器复位时序用它的测试套件验证，`ppu-2c02.cpp`、`apu-2a03.cpp`
  里多处注明「按 blargg 的 xxx 测试调定」。
- **[NESdev Wiki](https://www.nesdev.org/wiki/)** — mapper 电路、PPU/APU 寄存器行为的
  通用资料。

### 第三方库

- **[QuickJS-ng](https://github.com/quickjs-ng/quickjs)** — 插件脚本引擎。
  Copyright (c) Fabrice Bellard、Charlie Gordon、Ben Noordhuis、Saúl Ibarra Corretgé。MIT。
- **[nlohmann/json](https://github.com/nlohmann/json)** — JSON 处理。Copyright (c) Niels Lohmann。MIT。
- **[miniz](https://github.com/richgel999/miniz)** — zlib 压缩。
  Copyright (c) RAD Game Tools and Valve Software、Rich Geldreich and Tenacious Software LLC。MIT。

### 工具链

- **[Emscripten](https://emscripten.org/)** — 把 C++ 核心编译成 WebAssembly。
- **[React](https://react.dev/)** / **[Vite](https://vitejs.dev/)** — 前端框架与构建工具。

### 硬件资料

- 各样机的 ROM 固件、软盘镜像、磁带等资料，来自散落于互联网的爱好者存档与真机dump。
  这些资料的原始版权归各厂商所有，本项目仅用于运行与验证模拟器。

## 许可证

本项目整体使用 **GPL-3.0**，见 [LICENSE](LICENSE)。

原因是本项目链接并分发两处 GPL 代码：

- **Ember 核心**（GPL-3.0-only，sungbin）—— 编译进同一个 `.wasm`，属衍生作品
- **em-fceux 渲染管线**（GPL-2.0-or-later，Valtteri "tsone" Heikkilä）—— 其声明为
  「GPL 第 2 版或任意更新版本」，因此可以并入 GPL-3.0 项目

前端代码同样以 GPL-3.0 发布。

第三方库（QuickJS-ng、nlohmann/json、miniz）为 MIT，与 GPL 兼容，各自版权与许可
保留在原文件里，详见[致谢](#致谢)。
