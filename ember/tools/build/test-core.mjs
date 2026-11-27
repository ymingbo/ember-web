/*
 * ------------------------------------------------------------------------
 * 名称：test-core.mjs
 * 说明：构建并运行 C++ 测试：先生成 Schema 代码并打包官方插件，再用 CMake 在 build/ember/core-test 中构建 Debug 版测试并经 CTest 运行。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-13
 * 备注：测试不链接 Node-API，只测试核心库；命令行里多出的参数原样交给 CTest，如 `npm run core:test -- -R DebugInspector`（只跑匹配的用例）、`-E ^Roms/`（排除测试 ROM 用例）。
 * ------------------------------------------------------------------------
 */

import { execFileSync } from 'node:child_process';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { generate } from '../schema-gen/schema-gen.mjs';
import { findCmake } from './find-cmake.mjs';
import { CORE_TEST_BUILD_DIRECTORY } from './build-paths.mjs';
import { packPlugins } from './pack-plugins.mjs';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '..', '..');

/** 运行命令，失败时抛出异常。 */
function run(command, args) {
  execFileSync(command, args, { cwd: root, stdio: 'inherit' });
}

try {
  generate(root);
  // ROM 测试用真实的 nrom 插件包装配卡带，所以先打包插件。
  packPlugins(root);
  const { cmake, ctest } = findCmake();
  run(cmake, ['-S', 'core', '-B', CORE_TEST_BUILD_DIRECTORY, '-DEMBER_BUILD_TESTS=ON', ...(process.platform === 'win32' ? [] : ['-DCMAKE_BUILD_TYPE=Debug'])]);
  run(cmake, ['--build', CORE_TEST_BUILD_DIRECTORY, '--config', 'Debug', '--parallel']);
  run(ctest, ['--test-dir', CORE_TEST_BUILD_DIRECTORY, '-C', 'Debug', '--output-on-failure', ...process.argv.slice(2)]);
} catch (error) {
  console.error(error instanceof Error ? error.message : String(error));
  process.exit(1);
}
