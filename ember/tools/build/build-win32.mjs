/*
 * ------------------------------------------------------------------------
 * 名称：build-win32.mjs
 * 说明：构建 Win32 原生外壳：生成 Schema 代码，用 v143 工具集配置并构建 Release 版，输出在 build/ember/win32/<各目标>/Release。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-19
 * 备注：参数 --target <名称> 只构建指定目标（可重复）。
 * ------------------------------------------------------------------------
 */

import { WIN32_BUILD_DIRECTORY } from './build-paths.mjs';
import { build, configure } from './win32-cmake.mjs';

/** 读取命令行里的 --target 参数。 */
function readTargets() {
  const targets = [];
  for (let index = 0; index < process.argv.length; index += 1) {
    if (process.argv[index] === '--target' && process.argv[index + 1]) targets.push(process.argv[index + 1]);
  }
  return targets;
}

try {
  configure(['-DEMBER_BUILD_TESTS=OFF']);
  build(WIN32_BUILD_DIRECTORY, 'Release', readTargets());
} catch (error) {
  console.error(error instanceof Error ? error.message : String(error));
  process.exit(1);
}
