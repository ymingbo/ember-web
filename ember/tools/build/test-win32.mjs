/*
 * ------------------------------------------------------------------------
 * 名称：test-win32.mjs
 * 说明：用 Win32 原生外壳的工具链（v143、目标 Windows 7）构建 Debug 版核心测试、控件库测试与原生应用测试并运行。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-19
 * 备注：命令行里多出的参数原样交给 CTest，如 `npm run win32:test -- -E ^Roms/`；测试输出在 build/ember/win32-test，与构建目录分开。
 * ------------------------------------------------------------------------
 */

import { packPlugins } from './pack-plugins.mjs';
import { WIN32_TEST_BUILD_DIRECTORY } from './build-paths.mjs';
import { build, configure, EMBER_ROOT, test } from './win32-cmake.mjs';

try {
  // ROM 测试用真实的 nrom 插件包装配卡带，所以先打包插件。
  packPlugins(EMBER_ROOT);
  configure(['-DEMBER_BUILD_TESTS=ON', `-B${WIN32_TEST_BUILD_DIRECTORY}`]);
  build(WIN32_TEST_BUILD_DIRECTORY, 'Debug', ['ember_core_tests', 'rujian_ui_tests', 'rujian_ui_gallery', 'ember_app_tests', 'ember_win']);
  test(WIN32_TEST_BUILD_DIRECTORY, 'Debug', process.argv.slice(2));
} catch (error) {
  console.error(error instanceof Error ? error.message : String(error));
  process.exit(1);
}
