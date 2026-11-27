# Ember Web —— WebAssembly 版 FC/NES 模拟器（支持学习机）

本目录是 Ember 模拟器的网页版移植工程。它**不修改**上游 `ember/` 目录的任何文件，
只读取 `ember/core/src` 与 `ember/core/third_party` 作为源码输入，自己组装出
WebAssembly 模块。

支持两种模式：
- **FC 卡带模式**：经典 FC/NES，加载 iNES ROM，通过 mapper 插件运行卡带游戏
- **学习机模式**：裕兴 / 小霸王 / 步步高 / 金字塔等整机机型，ROM 内置在主板上

```
ember-web-full/
├── ember/            上游源码（与云盘一致，原样保留）
│   ├── core/         核心 C++ 源码 + third_party
│   ├── plugins/      234 个 mapper 插件（会被打进 wasm 的虚拟文件系统 /plugins）
│   ├── schema/       契约 schema
│   └── tools/        schema-gen 等构建脚本
├── ember-web/        本移植工程
│   ├── CMakeLists.txt        Web 版构建入口
│   ├── src/platform/web/    适配层（C API + Embind 绑定）
│   ├── scripts/              构建脚本
│   ├── docs/                移植说明
│   └── web/                 React + Vite 前端
└── README.md
```

## 一、准备工作

1. 安装 **Emscripten SDK**（建议 3.1.61 或更新版本）并激活：

   ```bash
   git clone https://github.com/emscripten-core/emsdk.git
   cd emsdk && ./emsdk install latest && source ./emsdk_env.sh
   ```

2. 安装 **Node.js**（18 以上，用于生成契约代码与构建前端）。

3. 生成契约代码（`src/contracts/*/generated`）。源码包里已包含生成结果，
   若重新拉取上游源码则需要执行一次：

   ```bash
   cd ember && node tools/schema-gen/schema-gen.mjs
   ```

## 二、编译 WebAssembly 模块

```bash
cd ember-web
bash scripts/build-wasm.sh
```

脚本做的事：

1. 跑 `node ../ember/tools/schema-gen/schema-gen.mjs` 生成契约代码；
2. `emcmake cmake -S .. -B build` + `emmake make -C build`；
3. 把 `ember_wasm.js` 复制到 `web/src/ember/`（供前端动态 import），
   把 `ember_wasm.wasm` 与 `ember_wasm.data` 复制到 `web/public/wasm/`；
4. 若 `web/dist/assets/` 已存在（前端已构建过），额外复制一份到该目录，
   避免生产环境 Vite 把 JS 放到 `/assets/` 下时 wasm 路径 404。

产物：

- `ember_wasm.js` —— Emscripten 胶水代码（ES Module，`MODULARIZE` + `EXPORT_ES6`）；
- `ember_wasm.wasm` —— 模拟器本体；
- `ember_wasm.data` —— 预加载的 mapper 插件（虚拟文件系统 `/plugins`，FC 卡带模式用）。

## 三、编译前端并本地预览

```bash
cd ember-web/web
npm install
npm run dev     # 开发模式 http://localhost:5173
npm run build   # 产出到 web/dist/，可直接丢到任意静态服务器
```

部署：`web/dist/` 是纯静态目录，用 Nginx / OSS / GitHub Pages 都可以托管。
`.wasm` 与 `.data` 需要以 `application/wasm` / `application/octet-stream` 返回，
并支持跨域（COEP 场景下需要 `Cross-Origin-Resource-Policy: cross-origin`）。

## 四、支持的机型

加载 ROM 时自动按 iNES mapper 号 + PRG 大小识别机型：

| 机型标识 | 名称 | mapper | PRG 大小 |
|---------|------|--------|----------|
| fc | 经典 FC/NES（卡带模式） | 其他 | — |
| yuxing | 裕兴学习机 | 169 | ≥ 1MB |
| sb97 | 小霸王 97 | 168 | ≥ 1MB |
| sb2000 | 小霸王 2000 | 171 | ≤ 512KB |
| bbg1 | 步步高软驱一号 | 171 | ≤ 128KB |
| bbg98 | 步步高 98 | 171 | > 512KB |
| pec9588 | 金字塔 PEC-9588 | 179 | ≤ 512KB |

FC 模式走 mapper 插件路线（`PluginRegistry` + `InesCartridgeLoader`），
学习机模式走整机机型路线（`MachineCatalog::createWithRom`，ROM 剥掉 iNES 头后直接传入）。

## 五、外设支持（v3 新增）

Web 版已完整接入上游的全部外设接口，学习机的键盘、软驱、打印机、磁带机都可以在浏览器里直接使用。

### 键盘

- 学习机模式下自动开启**全键盘透传**：所有物理键盘按键（按 `KeyboardEvent.code`）直接转发给模拟器
- 同时保留 FC 手柄按键映射（方向键/Z/X/Shift/Enter），学习机上玩 FC 游戏不受影响
- 对应 API：`sendKey(code, down)` / `hasKeyboard()`

### 鼠标

- 支持鼠标的机型自动启用画面鼠标捕获
- 鼠标坐标自动从屏幕像素映射到 256×240 帧坐标
- 支持左键、右键、中键、滚轮、鼠标离开事件
- 对应 API：`sendMouse(kind, x, y, dx, dy, buttons, wheel)` / `hasMouse()`

### 软驱（磁盘）

- 支持多软驱机型，自动显示对应数量的驱动器
- 支持插入 `.img` / `.dsk` / `.fdi` / `.td0` / `.86f` 格式的磁盘镜像
- 实时显示盘片状态（已插入 / 已修改）
- 修改后的盘片可以保存下载（写回本地）
- 对应 API：`floppyDriveCount()` / `floppyInsert()` / `floppyEject()` / `floppyInserted()` / `floppyModified()` / `floppyGetImage()`

### 打印机

- 支持打印机的机型自动启用打印输出捕获
- 实时统计已输出页数，点击即可预览最后一页打印内容
- 1 位位图渲染（白底黑字，200DPI 效果）
- 对应 API：`hasPrinter()` / `printerPagesOutput()` / `printerGetLastPage()`

### 磁带机

- 支持磁带机的机型可载入磁带音频
- 支持 WAV / MP3 / OGG / FLAC 格式，自动重采样到 96kHz 16-bit PCM
- 倒带功能（回到磁带开头）
- 实时显示播放状态
- 对应 API：`hasTape()` / `tapeLoad()` / `tapeRewind()` / `tapeIsPlaying()`

## 六、手动编译（不用脚本）

```bash
cd ember-web
emcmake cmake -B build -DCMAKE_BUILD_TYPE=Release
emmake make -C build -j2
```

## 七、这份 Web 版与桌面版的差别

Web 版**不编译**下面这些桌面端实现（`ember-web/CMakeLists.txt` 里的
`EMBER_WEB_EXCLUDED_SOURCES` 列出了完整清单与原因）：

- `platform/audio/miniaudio-output.cpp` —— 系统音频输出设备；Web 版改用 Web Audio API。
- `infra/media/disk-tape-store.cpp` —— 磁带声音解码，需要 FFmpeg（第三方库里没有 Web 版本）。
- `infra/output/debug-text-file-writer.cpp` —— 往本机文件写调试日志。
- `platform/network/tcp-socket.cpp`、`platform/network/websocket.cpp` —— 本机 TCP 调试服务与
  WebSocket 服务端。
- `platform/threading/thread-pool.cpp` —— 线程池；Web 版单线程跑。
- `infra/debug/debug-host.cpp`、`debug-server.cpp`、`debug-session.cpp` —— 调试服务。
- `logic/media/media-manager.cpp` —— 软驱/磁带管理，依赖本机文件与后台线程。
- `logic/runtime/simulation-runtime.cpp` —— 后台模拟线程。
- `bootstrap/core.cpp` 与 `bootstrap/modules/{output,media,debug,runtime}.cpp` —— 模块装配层，
  Web 版直接用 `Machine` 层，不走 RequestRouter / ThreadPool 那套多线程架构。

Web 适配层（`ember-web/src/platform/web/`）有三个文件：

- `ember_web_api.hpp` —— C API 头文件。
- `ember_web_api.cpp` —— C 接口实现：自动识别机型，FC 走 `FcMachine` +
  `InesCartridgeLoader` + `PluginRegistry`，学习机走 `MachineCatalog::createWithRom`。
  逐帧跑 `runFrame()`，从 `takeAudio()` 取音频采样，从 `frame()` 取 RGBA 帧缓冲。
- `wasm_bindings.cpp` —— Embind 绑定。

跨 wasm 边界的缓冲区一律传 **wasm 内存地址**，JS 侧用
`Module.HEAPU8` / `Module.HEAPF32` 读写；不要把 JS 数组的 `byteOffset`
当成 wasm 地址（两者不是一回事，这是这个版本修掉的一个真实内存越界隐患）。

## 八、编译注意事项

- **C++ 异常**：CMakeLists.txt 已开启 `-fexceptions` 和
  `-sDISABLE_EXCEPTION_CATCHING=0`。内核抛的 `CoreError` 会被 catchAll() 捕获并
  转为可读的错误文本；如果关掉异常支持，所有错误都会变成随机数字（wasm abort 的堆地址）。
- **PAGE_SIZE 宏冲突**：Emscripten sysroot 的 `bits/limits.h` 定义了 `PAGE_SIZE` 宏，
  会与源码里的 `static constexpr std::size_t PAGE_SIZE` 冲突。编译选项加了 `-UPAGE_SIZE`
  取消该宏定义。
- **Emscripten 版本**：已在 3.1.62 验证。`MODULARIZE_INSTANCE_EXPORTS` 是新版才有的设置，
  3.1.x 系列不支持，已移除。
