/*
 * ------------------------------------------------------------------------
 * 名称：pack-win32-probe.mjs
 * 说明：生成拿到 Windows 7 上运行的验证包：平台探针、导出表转储工具、DLL 清单、运行脚本与说明。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-19
 * 备注：包在 build/ember/win32/win7-package；DLL 清单是当前可执行文件导入的 DLL 与 win32/baseline/dll-list.txt（预先登记的、以后可能用到的系统 DLL）的并集；
 *       要先运行 npm run win32:build 与 npm run win32:check -- --list-only。
 * ------------------------------------------------------------------------
 */

import { copyFileSync, existsSync, mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { WIN32_BUILD_DIRECTORY } from './build-paths.mjs';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '..', '..');
/** 包的输出目录。 */
const PACKAGE_DIRECTORY = join(WIN32_BUILD_DIRECTORY, 'win7-package');
/** 要拷进包里的可执行文件。 */
const EXECUTABLES = [join(WIN32_BUILD_DIRECTORY, 'probe', 'Release', 'ember-probe.exe'), join(WIN32_BUILD_DIRECTORY, 'tools', 'export-dump', 'Release', 'ember-export-dump.exe')];

/** 运行脚本：只用英文提示，避免旧系统的控制台代码页显示乱码。 */
const RUN_SCRIPT = [
  '@echo off',
  'cd /d "%~dp0"',
  'echo [1/2] ember-probe.exe',
  'ember-probe.exe',
  'echo.',
  'echo [2/2] ember-export-dump.exe',
  'ember-export-dump.exe dll-list.txt win7-sp1-exports.json',
  'echo.',
  'echo Done. Copy ember-probe-report.txt and win7-sp1-exports.json back.',
  'pause',
  ''
].join('\r\n');

/** 说明文件（UTF-8 带标记，记事本能直接打开）。 */
const README = [
  'Ember 原生外壳的 Windows 7 验证包',
  '',
  '1. 把整个文件夹拷到干净的 Windows 7 SP1 虚拟机里（不要装任何补丁、运行库或 DirectX 组件）。',
  '2. 双击 run-on-win7.cmd，等它运行完。',
  '3. 把文件夹里生成的两个文件拷回来：',
  '   ember-probe-report.txt   平台探针的检查报告',
  '   win7-sp1-exports.json    系统 DLL 的导出表基线',
  '4. 如果程序弹出“找不到入口点”之类的错误窗口，把窗口截图一起带回来。',
  ''
].join('\r\n');

try {
  for (const file of EXECUTABLES) {
    if (!existsSync(file)) throw new Error(`没有找到 ${file}，先运行 npm run win32:build`);
  }
  const importedFile = join(WIN32_BUILD_DIRECTORY, 'imported-dlls.txt');
  if (!existsSync(importedFile)) throw new Error('没有导入清单，先运行 npm run win32:check -- --list-only');

  const names = new Map();
  const lists = [readFileSync(importedFile, 'utf8'), readFileSync(join(root, 'win32', 'baseline', 'dll-list.txt'), 'utf8')];
  for (const text of lists) {
    for (const line of text.split(/\r?\n/)) {
      const name = line.trim();
      if (name) names.set(name.toLowerCase(), name.toLowerCase());
    }
  }

  mkdirSync(PACKAGE_DIRECTORY, { recursive: true });
  for (const file of EXECUTABLES) copyFileSync(file, join(PACKAGE_DIRECTORY, file.split(/[\\/]/).pop()));
  writeFileSync(join(PACKAGE_DIRECTORY, 'dll-list.txt'), `${[...names.keys()].toSorted().join('\r\n')}\r\n`);
  writeFileSync(join(PACKAGE_DIRECTORY, 'run-on-win7.cmd'), RUN_SCRIPT);
  writeFileSync(join(PACKAGE_DIRECTORY, 'README.txt'), `\uFEFF${README}`);
  console.log(`验证包已生成：${PACKAGE_DIRECTORY}（${names.size} 个 DLL）`);
} catch (error) {
  console.error(error instanceof Error ? error.message : String(error));
  process.exit(1);
}
