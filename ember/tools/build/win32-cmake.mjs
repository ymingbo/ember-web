/*
 * ------------------------------------------------------------------------
 * 名称：win32-cmake.mjs
 * 说明：Win32 原生外壳构建脚本共用的函数：用 CMake 预设配置、构建与运行测试；配置前先生成 Schema 代码与第三方依赖清单。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-19
 * 备注：Windows 上使用 Visual Studio 自带的 CMake；配置、工具集与输出目录都由 win32/CMakePresets.json 的预设决定，输出在 build/ember/win32（见 build-paths.mjs）。
 * ------------------------------------------------------------------------
 */

import { execFileSync } from 'node:child_process';
import { readFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { generate } from '../schema-gen/schema-gen.mjs';
import { writeThirdPartyFiles } from '../third-party/third-party.mjs';
import { findCmake } from './find-cmake.mjs';
import { packResources } from './pack-resources.mjs';

/** ember 目录。 */
export const EMBER_ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..', '..');
/** 使用的 CMake 预设名。 */
const PRESET = 'win7-x64';

/**
 * 运行命令，失败时抛出异常。
 * @param {string} command 可执行文件。
 * @param {string[]} args 参数。
 * @param {string} cwd 工作目录。
 */
function run(command, args, cwd) {
  execFileSync(command, args, { cwd, stdio: 'inherit' });
}

/**
 * 生成核心需要的 Schema 代码并配置工程。
 * @param {string[]} extraArguments 追加给 CMake 配置命令的参数。
 */
export function configure(extraArguments = []) {
  if (process.platform !== 'win32') throw new Error('Win32 原生外壳只能在 Windows 上构建');
  const version = JSON.parse(readFileSync(join(EMBER_ROOT, 'package.json'), 'utf8')).version;
  generate(EMBER_ROOT);
  writeThirdPartyFiles();
  // 随程序分发的资源（预装插件、帮助文档、许可全文）打成一个 zip，由 CMake 嵌进 ember-win.exe；还没有构建出来的部分跳过
  const resources = packResources();
  if (resources.skipped.length > 0) console.log(`资源包里没有：${resources.skipped.join('、')}（先运行 npm run plugins:pack 与 npm run help:build）`);
  const { cmake } = findCmake();
  run(cmake, ['--preset', PRESET, `-DEMBER_CORE_VERSION=${version}`, `-DEMBER_RESOURCE_ARCHIVE=${resources.file}`, ...extraArguments], join(EMBER_ROOT, 'win32'));
}

/**
 * 构建工程。
 * @param {string} buildDirectory 构建目录。
 * @param {string} configuration 配置，Release 或 Debug。
 * @param {string[]} targets 要构建的目标，空数组表示全部。
 */
export function build(buildDirectory, configuration, targets = []) {
  const { cmake } = findCmake();
  run(cmake, ['--build', buildDirectory, '--config', configuration, '--parallel', ...(targets.length > 0 ? ['--target', ...targets] : [])], EMBER_ROOT);
}

/**
 * 运行 CTest。
 * @param {string} buildDirectory 构建目录。
 * @param {string} configuration 配置。
 * @param {string[]} extraArguments 追加给 CTest 的参数。
 */
export function test(buildDirectory, configuration, extraArguments = []) {
  const { ctest } = findCmake();
  run(ctest, ['--test-dir', buildDirectory, '-C', configuration, '--output-on-failure', ...extraArguments], EMBER_ROOT);
}
