/*
 * ------------------------------------------------------------------------
 * 名称：find-cmake.mjs
 * 说明：查找构建 C++ 核心用的 CMake：Windows 上使用 Visual Studio 自带的 CMake（默认生成 MSVC 工程），其他平台使用 PATH 中的 cmake。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-13
 * 备注：环境变量 EMBER_CMAKE 可指定 cmake 可执行文件；避免 PATH 中其他工具链（如 MinGW）自带的 CMake 选错编译器。
 * ------------------------------------------------------------------------
 */

import { execFileSync } from 'node:child_process';
import { existsSync } from 'node:fs';
import { dirname, join } from 'node:path';

/** Visual Studio 中 CMake 的相对位置。 */
const VS_CMAKE = join('Common7', 'IDE', 'CommonExtensions', 'Microsoft', 'CMake', 'CMake', 'bin', 'cmake.exe');

/**
 * 查找装有 C++ 工具的 Visual Studio（只用于 Windows）。
 * @returns {string} 安装目录；没有时为空字符串。
 */
export function findVisualStudio() {
  const vswhere = join(process.env['ProgramFiles(x86)'] ?? 'C:\\Program Files (x86)', 'Microsoft Visual Studio', 'Installer', 'vswhere.exe');
  if (!existsSync(vswhere)) return '';
  return execFileSync(vswhere, ['-latest', '-products', '*', '-requires', 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath'], { encoding: 'utf8' }).trim();
}

/**
 * 查找 cmake 与 ctest。
 * @returns {{ cmake: string, ctest: string }} 可执行文件路径。
 */
export function findCmake() {
  const override = process.env.EMBER_CMAKE;
  if (override) return { cmake: override, ctest: join(dirname(override), process.platform === 'win32' ? 'ctest.exe' : 'ctest') };
  if (process.platform !== 'win32') return { cmake: 'cmake', ctest: 'ctest' };

  const installation = findVisualStudio();
  if (!installation) throw new Error('没有找到 Visual Studio，请安装带“使用 C++ 的桌面开发”工作负载的 Visual Studio，或用环境变量 EMBER_CMAKE 指定 cmake');
  const cmake = join(installation, VS_CMAKE);
  if (!existsSync(cmake)) throw new Error('Visual Studio 中没有 C++ 的 CMake 工具，请在安装程序中勾选“用于 Windows 的 C++ CMake 工具”，或用环境变量 EMBER_CMAKE 指定 cmake');
  return { cmake, ctest: join(dirname(cmake), 'ctest.exe') };
}
