#!/bin/bash
# ------------------------------------------------------------------------
# 名称：build-all.sh
# 说明：完整构建：WASM + 前端
# 用法：./scripts/build-all.sh
# ------------------------------------------------------------------------

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

echo "========================================"
echo "Ember Web - 完整构建"
echo "========================================"

# 1. 构建 WASM
echo ""
echo "=== 步骤 1/2: 构建 WASM ==="
bash "$SCRIPT_DIR/build-wasm.sh" Release

# 2. 构建前端
echo ""
echo "=== 步骤 2/2: 构建前端 ==="
cd "$PROJECT_ROOT/web"

if [ ! -d "node_modules" ]; then
  echo "安装依赖..."
  npm install
fi

echo "构建前端..."
npm run build

echo ""
echo "========================================"
echo "全部构建完成！"
echo "前端输出: $PROJECT_ROOT/web/dist"
echo "部署方式: 将 dist/ 目录部署到任意静态文件服务器"
echo "========================================"
