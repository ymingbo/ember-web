/*
 * ------------------------------------------------------------------------
 * 名称：check-win32-imports.mjs
 * 说明：Windows 7 兼容检查：列出 Win32 原生外壳的可执行文件导入的全部系统函数，对照 Windows 7 SP1 的导出基线，出现基线里没有的导入就报错。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-19
 * 备注：可执行文件已经链接了核心与第三方库，所以导入表就是整个程序对系统的全部依赖；用 v143 工具集自带的 dumpbin 读取。
 *       基线 win32/baseline/win7-sp1-exports.json 由 win32/tools/export-dump 在干净的 Windows 7 SP1 上生成；还没有基线时只输出导入清单并以失败退出。
 *       参数：`--list-only` 只写导入清单与 DLL 清单；其余参数是要检查的可执行文件，默认检查原生应用、probe、export-dump 与控件库画廊。
 * ------------------------------------------------------------------------
 */

import { execFileSync } from 'node:child_process';
import { existsSync, mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { findVisualStudio } from '../build/find-cmake.mjs';
import { WIN32_BUILD_DIRECTORY } from '../build/build-paths.mjs';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '..', '..');
/** Windows 7 SP1 导出基线的位置。 */
const BASELINE_FILE = join(root, 'win32', 'baseline', 'win7-sp1-exports.json');
/** 默认检查的可执行文件。 */
const DEFAULT_TARGETS = [
  join(WIN32_BUILD_DIRECTORY, 'app', 'Release', 'ember-win.exe'),
  join(WIN32_BUILD_DIRECTORY, 'probe', 'Release', 'ember-probe.exe'),
  join(WIN32_BUILD_DIRECTORY, 'tools', 'export-dump', 'Release', 'ember-export-dump.exe'),
  join(WIN32_BUILD_DIRECTORY, 'ui-kit', 'Release', 'rujian-ui-gallery.exe')
];

/**
 * 找到 v143 工具集自带的 dumpbin。
 * @returns {string} dumpbin.exe 的路径。
 */
function findDumpbin() {
  const installation = findVisualStudio();
  if (!installation) throw new Error('没有找到 Visual Studio');
  const version = readFileSync(join(installation, 'VC', 'Auxiliary', 'Build', 'Microsoft.VCToolsVersion.v143.default.txt'), 'utf8').trim();
  const dumpbin = join(installation, 'VC', 'Tools', 'MSVC', version, 'bin', 'Hostx64', 'x64', 'dumpbin.exe');
  if (!existsSync(dumpbin)) throw new Error(`没有找到 ${dumpbin}`);
  return dumpbin;
}

/**
 * 解析 dumpbin /imports 的输出。
 * @param {string} text dumpbin 的输出。
 * @param {Map<string, Set<string>>} imports 累加结果：DLL 名称（小写）到函数名集合，按序号导入的记成“#序号”。
 */
function parseImports(text, imports) {
  let current = null;
  for (const line of text.split(/\r?\n/)) {
    // 输出末尾的“Summary”是各节的大小，不是导入。
    if (/^\s*Summary\s*$/.test(line)) break;
    const dll = /^ {4}(\S+\.dll)\s*$/i.exec(line);
    if (dll) {
      current = dll[1].toLowerCase();
      if (!imports.has(current)) imports.set(current, new Set());
      continue;
    }
    if (current === null) continue;
    const ordinal = /^\s+Ordinal\s+(\d+)\s*$/.exec(line);
    if (ordinal) {
      imports.get(current).add(`#${ordinal[1]}`);
      continue;
    }
    const named = /^\s{8,}([0-9A-Fa-f]+)\s+(\S+)\s*$/.exec(line);
    if (named) imports.get(current).add(named[2]);
  }
}

/**
 * 读取一个可执行文件的导入。
 * @param {string} dumpbin dumpbin 路径。
 * @param {string} file 可执行文件。
 * @param {Map<string, Set<string>>} imports 累加结果。
 */
function readImports(dumpbin, file, imports) {
  const output = execFileSync(dumpbin, ['/nologo', '/imports', file], { encoding: 'utf8', maxBuffer: 64 * 1024 * 1024 });
  parseImports(output, imports);
}

/**
 * 对照基线，找出在 Windows 7 SP1 上不存在的导入。
 * @param {Map<string, Set<string>>} imports 全部导入。
 * @param {{ os: string, exports: Record<string, string[]>, missing: string[] }} baseline 基线。
 * @returns {string[]} 违规说明。
 */
function findViolations(imports, baseline) {
  const missingDlls = new Set(baseline.missing.map((name) => name.toLowerCase()));
  const exported = new Map(Object.entries(baseline.exports).map(([name, functions]) => [name.toLowerCase(), new Set(functions)]));
  const violations = [];
  for (const [dll, functions] of imports) {
    if (missingDlls.has(dll)) {
      violations.push(`${dll}：Windows 7 SP1 上没有这个 DLL（导入了 ${functions.size} 个函数）`);
      continue;
    }
    const available = exported.get(dll);
    if (!available) {
      violations.push(`${dll}：基线里没有这个 DLL，把它加进 DLL 清单后在 Windows 7 上重新导出基线`);
      continue;
    }
    for (const name of functions) {
      if (name.startsWith('#')) continue;
      if (!available.has(name)) violations.push(`${dll}!${name}：Windows 7 SP1 上没有这个函数`);
    }
  }
  return violations;
}

try {
  const args = process.argv.slice(2);
  const listOnly = args.includes('--list-only');
  const targets = args.filter((arg) => !arg.startsWith('--'));
  const files = targets.length > 0 ? targets : DEFAULT_TARGETS;
  for (const file of files) {
    if (!existsSync(file)) throw new Error(`没有找到 ${file}，先运行 npm run win32:build`);
  }

  const dumpbin = findDumpbin();
  const imports = new Map();
  for (const file of files) readImports(dumpbin, file, imports);

  const sorted = [...imports.keys()].toSorted();
  mkdirSync(WIN32_BUILD_DIRECTORY, { recursive: true });
  writeFileSync(join(WIN32_BUILD_DIRECTORY, 'imported-dlls.txt'), `${sorted.join('\r\n')}\r\n`);
  writeFileSync(join(WIN32_BUILD_DIRECTORY, 'imports.json'), `${JSON.stringify(Object.fromEntries(sorted.map((name) => [name, [...imports.get(name)].toSorted()])), null, 2)}\n`);
  const total = [...imports.values()].reduce((sum, set) => sum + set.size, 0);
  console.log(`导入了 ${sorted.length} 个 DLL、${total} 个函数，清单在 ${WIN32_BUILD_DIRECTORY}`);
  if (listOnly) process.exit(0);

  if (!existsSync(BASELINE_FILE)) {
    console.error(`没有 Windows 7 SP1 的导出基线 ${BASELINE_FILE}：运行 npm run win32:probe-package，把生成的验证包拷到干净的 Windows 7 SP1 上运行，生成的 win7-sp1-exports.json 放到这个位置`);
    process.exit(1);
  }
  const violations = findViolations(imports, JSON.parse(readFileSync(BASELINE_FILE, 'utf8')));
  if (violations.length > 0) {
    console.error(`有 ${violations.length} 项导入在 Windows 7 SP1 上不存在：\n${violations.join('\n')}`);
    process.exit(1);
  }
  console.log('全部导入都在 Windows 7 SP1 的导出基线里');
} catch (error) {
  console.error(error instanceof Error ? error.message : String(error));
  process.exit(1);
}
