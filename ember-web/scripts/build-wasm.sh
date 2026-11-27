#!/usr/bin/env bash
# ------------------------------------------------------------------------
# 名称：build-wasm.sh
# 说明：构建 Ember WebAssembly 模块，并把产物放到前端工程里。
# ------------------------------------------------------------------------
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WEB_DIR="$HERE/../web"
BUILD_DIR="${BUILD_DIR:-$HERE/../build}"
OUT_DIR="$WEB_DIR/public/wasm"

if ! command -v emcmake >/dev/null 2>&1; then
  echo "找不到 emcmake：请先安装并激活 Emscripten SDK（source emsdk_env.sh）。" >&2
  exit 1
fi

echo "==> 生成契约代码（src/contracts/*/generated）"
node "$HERE/../../ember/tools/schema-gen/schema-gen.mjs"

echo "==> 配置 CMake"
emcmake cmake -S "$HERE/.." -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release

echo "==> 编译"
emmake make -C "$BUILD_DIR" -j2

mkdir -p "$OUT_DIR" "$WEB_DIR/src/ember"
# Emscripten 胶水代码进 src/，让前端能动态 import；.wasm 与 .data 走 public/wasm/
cp "$BUILD_DIR/ember_wasm.js" "$WEB_DIR/src/ember/ember_wasm.js"
cp "$BUILD_DIR/ember_wasm.wasm" "$OUT_DIR/"
# --preload-file 生成的 .data 也要一起复制（mapper 插件在虚拟文件系统的 /plugins 下）
if [ -f "$BUILD_DIR/ember_wasm.data" ]; then
  cp "$BUILD_DIR/ember_wasm.data" "$OUT_DIR/"
fi

# Vite 构建时会把 JS 放到 /assets/ 下，emscripten 胶水代码会从同路径找 .wasm/.data
# 因此额外复制一份到 dist/assets/，避免生产环境 404
DIST_ASSETS="$WEB_DIR/dist/assets"
if [ -d "$DIST_ASSETS" ]; then
  cp "$BUILD_DIR/ember_wasm.wasm" "$DIST_ASSETS/" 2>/dev/null || true
  if [ -f "$BUILD_DIR/ember_wasm.data" ]; then
    cp "$BUILD_DIR/ember_wasm.data" "$DIST_ASSETS/" 2>/dev/null || true
  fi
  echo "    已同步到 dist/assets/"
fi

echo "==> 产物已就位"
ls -la "$OUT_DIR"
ls -la "$WEB_DIR/src/ember"
