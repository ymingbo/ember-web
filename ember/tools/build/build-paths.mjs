/*
 * ------------------------------------------------------------------------
 * 名称：build-paths.mjs
 * 说明：Ember 构建产物的目录：全部放在仓库根目录的 build/ember 下，不进入 ember/ 目录。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-17
 * 备注：帮助文档的 VitePress 配置（help/.vitepress/config.mts）直接引用这里的 HELP_BUILD_DIRECTORY。
 * ------------------------------------------------------------------------
 */

import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

/** 构建产物的根目录：<仓库根>/build/ember。 */
export const BUILD_ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..', '..', '..', 'build', 'ember');
/** C++ 测试的构建目录。 */
export const CORE_TEST_BUILD_DIRECTORY = join(BUILD_ROOT, 'core-test');
/** Win32 原生外壳的构建目录（与 win32/CMakePresets.json 的 binaryDir 一致）。 */
export const WIN32_BUILD_DIRECTORY = join(BUILD_ROOT, 'win32');
/** 用 Win32 原生外壳的工具链构建核心测试的目录。 */
export const WIN32_TEST_BUILD_DIRECTORY = join(BUILD_ROOT, 'win32-test');
/** Win32 控件库与网页版对照的输出目录：网页版参考页、两边的截图与对照报告。 */
export const WIN32_REFERENCE_DIRECTORY = join(BUILD_ROOT, 'win32-reference');
/** 打好的预装插件包目录。 */
export const PLUGINS_BUILD_DIRECTORY = join(BUILD_ROOT, 'plugins');
/** 第三方开源代码许可全文的输出目录。 */
export const THIRD_PARTY_BUILD_DIRECTORY = join(BUILD_ROOT, 'third-party');
/** 帮助文档站点（help/ 由 VitePress 构建）的输出目录。 */
export const HELP_BUILD_DIRECTORY = join(BUILD_ROOT, 'help');
/** 随程序分发的资源包（预装插件、帮助文档、许可全文）的输出目录，CMake 把它嵌进 ember-win.exe。 */
export const RESOURCES_BUILD_DIRECTORY = join(BUILD_ROOT, 'resources');
/** 发布包（可执行文件与压缩包）的输出目录。 */
export const RELEASE_BUILD_DIRECTORY = join(BUILD_ROOT, 'release');
