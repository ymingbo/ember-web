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

### 1. 准备工具链

需要 **Emscripten SDK**（3.1.62 验证过）和 **Node.js**（18 以上）。

```bash
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk && ./emsdk install latest && source ./emsdk_env.sh
```

### 2. 编译 WebAssembly 模块

```bash
cd ember-web
bash scripts/build-wasm.sh
```

产物会复制到 `web/src/ember/`（胶水 JS）和 `web/public/wasm/`（wasm 与 data）。

### 3. 编译前端并运行

```bash
cd web
npm install
npm run dev     # 开发模式 http://localhost:5173
npm run build   # 产出到 web/dist/，纯静态，可直接部署
```

部署时注意 `.wasm` 要以 `application/wasm` 返回，`.data` 以 `application/octet-stream` 返回。

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

## 许可证

本项目整体使用 **GPL-3.0**，见 [LICENSE](LICENSE)。

原因：本项目链接并分发 GPL-3.0 的 Ember 核心（编译进同一个 `.wasm`），属于衍生作品，
必须同样以 GPL-3.0 分发。前端代码也不例外。

原始 Ember 核心版权归 sungbin 所有，许可为 GPL-3.0-only。
