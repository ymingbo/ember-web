/*
 * ------------------------------------------------------------------------
 * 名称：third-party.mjs
 * 说明：Ember 使用的第三方开源代码清单：读取 core/third_party、win32/ui-kit/third_party 与 help/third_party 下各项目目录的 third-party.json 与许可文件，
 *       加上帮助文档站点随程序分发的页面框架（npm 包本身）；生成“关于”对话框用的 C++ 清单源文件，并写出随程序分发的许可全文。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-21
 * 备注：不依赖别的仓库；声明或许可缺失时直接报错，构建随之失败；直接运行（node tools/third-party/third-party.mjs）时写出两个生成物，
 *       win32-cmake.mjs 在配置工程前调用 writeThirdPartyFiles；清单见 docs/01-软件架构设计.md 第 10 节。
 * ------------------------------------------------------------------------
 */

import { existsSync, mkdirSync, readdirSync, readFileSync, statSync, writeFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { THIRD_PARTY_BUILD_DIRECTORY } from '../build/build-paths.mjs';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..', '..');
/** 每个第三方目录里的声明文件名。 */
export const DECLARATION_FILE = 'third-party.json';
/** 许可全文文件名，随程序分发。 */
export const NOTICES_FILE = 'THIRD_PARTY_NOTICES.txt';
/** 许可全文的输出目录。 */
export const NOTICES_DIRECTORY = THIRD_PARTY_BUILD_DIRECTORY;
/** 清单 C++ 源文件的位置（生成物，不提交）。 */
export const NOTICES_SOURCE = join(ROOT, 'win32', 'app', 'src', 'bootstrap', 'generated', 'third-party-notices.generated.cpp');
/** 第三方目录。 */
const THIRD_PARTY_DIRECTORIES = [join(ROOT, 'core', 'third_party'), join(ROOT, 'win32', 'ui-kit', 'third_party'), join(ROOT, 'help', 'third_party')];
/** 帮助文档站点（help/，由 VitePress 构建）随程序分发的页面框架：只取这些包本身，构建工具（VitePress 自己的依赖、代码高亮等）不进程序。 */
const HELP_SITE_PACKAGES = ['vitepress', 'vue', '@vue/runtime-dom', '@vue/runtime-core', '@vue/reactivity', '@vue/shared', '@vue/devtools-api', '@vueuse/core', '@vueuse/shared', '@vueuse/integrations', 'focus-trap', 'tabbable', 'minisearch', 'mark.js'];
const HELP_SITE_USAGE = '帮助文档站点的页面框架（随程序分发的帮助文档里用到）';
/** 包目录里可能的许可文件名（不区分大小写，按前缀匹配）。 */
const LICENSE_FILE_PATTERN = /^(licen[cs]e|copying)(\.|$|-)/i;

/**
 * 读取第三方目录下各项目的声明。
 * @param {string} directory third_party 目录。
 * @returns {object[]} 清单条目（含许可全文）。
 */
export function readDeclarations(directory) {
  if (!existsSync(directory)) return [];
  return readdirSync(directory)
    .filter((name) => statSync(join(directory, name)).isDirectory())
    .map((name) => {
      const folder = join(directory, name);
      const file = join(folder, DECLARATION_FILE);
      if (!existsSync(file)) throw new Error(`第三方目录 ${folder} 缺少 ${DECLARATION_FILE}`);
      const data = JSON.parse(readFileSync(file, 'utf8'));
      for (const key of ['name', 'license', 'licenseFile', 'homepage', 'usage']) {
        if (typeof data[key] !== 'string' || data[key].trim() === '') throw new Error(`${file} 缺少 ${key}`);
      }
      if (!Array.isArray(data.authors) || data.authors.length === 0) throw new Error(`${file} 缺少 authors`);
      const licensePath = join(folder, data.licenseFile);
      if (!existsSync(licensePath)) throw new Error(`${file} 声明的许可文件 ${data.licenseFile} 不存在`);
      return {
        name: data.name,
        version: data.version ?? '',
        authors: data.authors,
        license: data.license,
        homepage: data.homepage,
        usage: data.usage,
        bundled: data.bundled !== false,
        licenseText: readFileSync(licensePath, 'utf8').trim()
      };
    });
}

/** 只读取一个已安装的 npm 包本身的信息（不遍历它的依赖），用于随程序分发的页面框架。 */
function readInstalledPackage(name, usage) {
  let folder = null;
  for (let dir = ROOT; folder === null; dir = dirname(dir)) {
    if (existsSync(join(dir, 'node_modules', name, 'package.json'))) folder = join(dir, 'node_modules', name);
    else if (dirname(dir) === dir) throw new Error(`找不到 npm 包 ${name}，请先安装依赖`);
  }
  const data = JSON.parse(readFileSync(join(folder, 'package.json'), 'utf8'));
  const licenseFile = readdirSync(folder).find((file) => LICENSE_FILE_PATTERN.test(file) && statSync(join(folder, file)).isFile());
  if (licenseFile === undefined) throw new Error(`npm 包 ${name} 没有许可文件`);
  const licenseText = readFileSync(join(folder, licenseFile), 'utf8').trim();
  const author = typeof data.author === 'string' ? data.author.replace(/\s*[<(].*$/, '') : (data.author?.name ?? '');
  const holder = /^copyright\s*(?:\(c\)|©)?\s*(?:\d{4}(?:\s*-\s*\d{4})?,?)?\s*(.+)$/im.exec(licenseText)?.[1]?.trim() ?? '';
  const authors = [author !== '' ? author : holder].filter((value) => value !== '');
  const license = typeof data.license === 'string' ? data.license : (data.license?.type ?? '');
  if (authors.length === 0 || license === '') throw new Error(`npm 包 ${name} 找不到作者或许可`);
  const repository = typeof data.repository === 'string' ? data.repository : data.repository?.url;
  return {
    name,
    version: data.version ?? '',
    authors,
    license,
    homepage: data.homepage ?? (repository ? repository.replace(/^git\+/, '').replace(/\.git$/, '') : ''),
    usage,
    bundled: true,
    licenseText
  };
}

/**
 * Ember 的第三方清单（含许可全文），按名称排序；不含不随程序分发的项。
 * @returns {object[]} 清单条目。
 */
export function collectNotices() {
  const entries = [...THIRD_PARTY_DIRECTORIES.flatMap(readDeclarations), ...HELP_SITE_PACKAGES.map((name) => readInstalledPackage(name, HELP_SITE_USAGE))].filter((entry) => entry.bundled);
  const unique = new Map(entries.map((entry) => [entry.name, entry]));
  return [...unique.values()].toSorted((a, b) => a.name.localeCompare(b.name, 'en', { sensitivity: 'base' }));
}

/**
 * 许可全文：每项一节，写名称、版本、作者、许可、主页、用途与许可全文。
 * @param {object[]} entries 清单条目。
 */
export function formatNotices(entries) {
  const line = '-'.repeat(72);
  const sections = entries.map((entry) =>
    [
      line,
      `${entry.name}${entry.version ? ` ${entry.version}` : ''}`,
      `作者：${entry.authors.join('、')}`,
      `许可：${entry.license}`,
      ...(entry.homepage ? [`主页：${entry.homepage}`] : []),
      ...(entry.usage ? [`用途：${entry.usage}`] : []),
      '',
      entry.licenseText
    ].join('\n')
  );
  return `Ember 使用的第三方开源代码\n\n${sections.join('\n\n')}\n`;
}

/** 写成 C++ 窄字符串字面量（源文件按 UTF-8 编译）：反斜杠、引号与控制字符转义，三字符序列里的问号拆开避免被当作三字符组。 */
function cppString(text) {
  let result = '"';
  for (const char of text) {
    const code = char.codePointAt(0);
    if (char === '\\') result += '\\\\';
    else if (char === '"') result += '\\"';
    else if (char === '\n') result += '\\n';
    else if (char === '\r') result += '\\r';
    else if (char === '\t') result += '\\t';
    else if (char === '?') result += '\\?';
    else if (code < 0x20 || code === 0x7f) result += `\\${code.toString(8).padStart(3, '0')}`;
    else result += char;
  }
  return `${result}"`;
}

/**
 * 清单的 C++ 源文件内容。
 * @param {object[]} entries 清单条目。
 */
export function formatNoticeSource(entries) {
  const rows = entries.map((entry) => `    {${[entry.name, entry.version, entry.authors.join('、'), entry.license, entry.homepage, entry.usage].map(cppString).join(', ')}},`);
  return [
    '// 由 tools/third-party/third-party.mjs 生成，不要手改，也不提交。',
    '',
    '#include "bootstrap/third-party-notices.hpp"',
    '',
    'namespace ember::win32::bootstrap {',
    '',
    'const std::vector<contracts::ThirdPartyNotice>& thirdPartyNotices() {',
    '  static const std::vector<contracts::ThirdPartyNotice> kNotices{',
    ...rows,
    '  };',
    '  return kNotices;',
    '}',
    '',
    '}  // namespace ember::win32::bootstrap',
    ''
  ].join('\n');
}

/**
 * 写出两个生成物：关于对话框用的 C++ 清单源文件与随程序分发的许可全文。
 * @returns {{ source: string, notices: string, count: number }} 两个文件的路径与清单项数。
 */
export function writeThirdPartyFiles() {
  const entries = collectNotices();
  mkdirSync(dirname(NOTICES_SOURCE), { recursive: true });
  writeIfChanged(NOTICES_SOURCE, formatNoticeSource(entries));
  mkdirSync(NOTICES_DIRECTORY, { recursive: true });
  const notices = join(NOTICES_DIRECTORY, NOTICES_FILE);
  writeIfChanged(notices, formatNotices(entries));
  return { source: NOTICES_SOURCE, notices, count: entries.length };
}

/**
 * 内容没有变化时不写文件，避免改动文件时间引起依赖它的目标重新编译。
 * @param {string} file 文件路径。
 * @param {string} text 要写入的文本。
 */
function writeIfChanged(file, text) {
  if (existsSync(file) && readFileSync(file, 'utf8') === text) return;
  writeFileSync(file, text);
}

if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const { count, notices } = writeThirdPartyFiles();
  console.log(`第三方清单：${count} 项，许可全文 ${notices}`);
}
