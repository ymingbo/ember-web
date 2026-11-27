# 关于这个目录

## 来源

本目录是 [sungbindev/famiclone](https://github.com/sungbindev/famiclone) 仓库中
`ember/` 子目录的**原样拷贝**，版本 0.3.9。

拷贝时未做任何修改，原样保留用于本项目的 WebAssembly 编译输入。

## 许可

GPL-3.0-only，版权归 sungbin 所有。许可全文见 [LICENSE](LICENSE)。

## 与原仓库的差异

只保留编译 Web 版所需的子集，未包含以下上游内容：

| 未包含 | 原因 |
|--------|------|
| `docs/`（设计文档） | 在上游仓库根目录，不属于 `ember/` 子目录 |
| `help/`（帮助文档） | 同上，用于 Win32 桌面版 |
| `win32/`（Windows 外壳） | Web 版不使用原生外壳 |
| `core/test/`（C++ 测试） | 测试依赖本机环境，Web 版不做 |
| 各种 `build-win32.ps1` 等脚本 | 仅 Windows 桌面版使用 |

因此 `README.md` 里指向 `docs/` 和 `help/` 的链接在本目录内是无效的，
需要查看这些文档请到[上游仓库](https://github.com/sungbindev/famiclone)。

同理，`package.json` 里的 `win32:*`、`vscode:check`、`help:build` 等脚本
指向的 `win32/`、`../vscode/` 目录也不在本仓库内，这些脚本无法执行。
**Web 版构建不使用 `ember/` 的 npm 脚本**，只需运行
`../ember-web/scripts/build-wasm.sh`，它直接调用 `tools/schema-gen/schema-gen.mjs`
生成契约代码后交给 CMake 编译。`ember/` 下无需 `npm install`。

## 上游的完整能力

Ember 本身是面向 Windows 的桌面模拟器（Win32 原生外壳 + C++ 核心）。
本项目只使用它的 `core/` 核心，通过 Web 适配层
（`../ember-web/src/platform/web/`）暴露给浏览器。

桌面版支持而 Web 版未实现的功能，在 `../ember-web/README.md` 的
「这份 Web 版与桌面版的差别」一节有完整清单。
