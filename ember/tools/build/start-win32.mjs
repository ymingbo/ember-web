/*
 * ------------------------------------------------------------------------
 * 名称：start-win32.mjs
 * 说明：以开发方式启动 Win32 原生外壳：生成 Schema 代码、打包预装插件、构建 Release 版的原生应用，再运行它，窗口关闭后返回。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-19
 * 备注：命令行里多出的参数原样交给应用，如 `npm run win32:start -- --debug-port=5688 --cartridge=D:\roms\game.nes`；
 *       用户数据用应用默认的目录，要隔离时加 --user-data-dir=目录。
 * ------------------------------------------------------------------------
 */

import { execFileSync } from 'node:child_process';
import { existsSync } from 'node:fs';
import { join } from 'node:path';
import { packPlugins } from './pack-plugins.mjs';
import { HELP_BUILD_DIRECTORY, WIN32_BUILD_DIRECTORY } from './build-paths.mjs';
import { build, configure, EMBER_ROOT } from './win32-cmake.mjs';

try {
  // 开发时预装插件取 build/ember/plugins，先打好
  packPlugins(EMBER_ROOT);
  // 帮助站点还没构建时先构建（要先生成 Schema 代码），否则资源包里没有帮助文档，菜单里的帮助打不开；已经构建过的不重建，要更新就运行 npm run help:build
  if (!existsSync(join(HELP_BUILD_DIRECTORY, 'index.html'))) {
    execFileSync(process.execPath, [join(EMBER_ROOT, 'tools', 'schema-gen', 'schema-gen.mjs')], { cwd: EMBER_ROOT, stdio: 'inherit' });
    execFileSync(process.execPath, [join(EMBER_ROOT, 'tools', 'build', 'build-help.mjs')], { cwd: EMBER_ROOT, stdio: 'inherit' });
  }
  configure(['-DEMBER_BUILD_TESTS=OFF']);
  build(WIN32_BUILD_DIRECTORY, 'Release', ['ember_win']);
  execFileSync(join(WIN32_BUILD_DIRECTORY, 'app', 'Release', 'ember-win.exe'), process.argv.slice(2), { stdio: 'inherit' });
} catch (error) {
  console.error(error instanceof Error ? error.message : String(error));
  process.exit(1);
}
