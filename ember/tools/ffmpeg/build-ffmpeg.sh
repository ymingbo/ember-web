#!/usr/bin/env bash
# ------------------------------------------------------------------------
# 名称：build-ffmpeg.sh
# 说明：在编译目录中运行 FFmpeg 的 configure 与 make，并把静态库与头文件安装到指定目录；由 build-ffmpeg.mjs 调用，参数依次为源码目录、编译目录、安装目录、并行数与 configure 参数。
# 作者：sungbin
# 邮箱：sungbin@3578.cn
# 日期：2026-11-20
# 备注：在 MSYS2 的 bash 中运行，环境变量来自 Visual Studio 的 vcvars64.bat。
# ------------------------------------------------------------------------

set -euo pipefail

source_dir="$1"
build_dir="$2"
install_dir="$3"
jobs="$4"
shift 4

if command -v cygpath >/dev/null 2>&1; then
  source_dir="$(cygpath -u "$source_dir")"
  build_dir="$(cygpath -u "$build_dir")"
  install_dir="$(cygpath -u "$install_dir")"
  # MSVC 的 link.exe 要排在 MSYS2 自带的 link 命令之前。
  PATH="$(cygpath -u "$VCToolsInstallDir")bin/Hostx64/x64:$PATH"
  export PATH
fi

cd "$build_dir"
"$source_dir/configure" --prefix="$install_dir" "$@"
make -j"$jobs"
make install-libs install-headers
