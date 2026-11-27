/*
 * ------------------------------------------------------------------------
 * 名称：import-fcdev-docs.mjs
 * 说明：把 fcdev 扩展的使用文档（含 SB2000 的 C 接口参考与符号参考）导入 Ember 帮助站点的 help/fcdev：按扩展的主题登记表（vscode/docs/topics.json）生成各页，改写文档之间的链接，并生成概述页。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-18
 * 备注：文档只在 vscode（fcdev 扩展）里维护，help/fcdev 不提交（ember/.gitignore），每次 npm run help:build 都会重新导入，并删除不再登记的页面。
 *       指向文档目录之外的链接（头文件、链接配置、目录）在帮助里打不开，改成普通文字；正文里的尖括号与双花括号要转义，否则 VitePress 会当成 Vue 模板编译；
 *       文档里单独成节的“## 目录”（长文档为单独阅读写的）去掉，帮助页右边有自动生成的页内大纲；
 *       站点的侧边栏分组与导入用同一份分组规则（sidebarGroups），保证两边一致。
 * ------------------------------------------------------------------------
 */

import { existsSync, mkdirSync, readFileSync, readdirSync, rmSync, writeFileSync } from 'node:fs';
import { dirname, join, relative, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
const here = dirname(fileURLToPath(import.meta.url));

/** 仓库根目录。 */
export const REPO_ROOT = resolve(here, '..', '..', '..');
/** fcdev 扩展目录。 */
export const EXTENSION_ROOT = join(REPO_ROOT, 'vscode');
/** fcdev 扩展的使用文档主题登记表（扩展与帮助站点读同一份）。 */
export const DOC_TOPICS = JSON.parse(readFileSync(join(EXTENSION_ROOT, 'docs', 'topics.json'), 'utf8')).topics;
/** 导入的目标目录（ember/help/fcdev）。 */
export const HELP_FCDEV_DIRECTORY = resolve(here, '..', '..', 'help', 'fcdev');

/** 用户可见的文字。 */
const TEXT = {
  title: 'fcdev 开发手册',
  overview: '概述',
  intro: 'fcdev 是 6502 兼容机（NES、小霸王学习机等）的 VS Code 扩展：用 cc65 把 C 或 ca65 汇编构建成机型对应的程序，可以在模拟器里运行与调试，也能把已有程序反汇编回 ca65 源码。这个扩展是 fcdev 唯一的开发环境，使用前先按“安装与入门”安装。命令“fcdev：运行”“fcdev：调试运行”与调试命令就是经 Ember 的调试服务运行与调试程序的，所以手册放在这里，直接就能查。',
  source: '手册随 fcdev 扩展一起发布，这里是同一份内容；在 VS Code 里用命令“fcdev：打开使用文档”也能查到。',
  columns: ['手册', '内容'],
  common: '通用',
  nes: 'NES',
  sb2000: 'SB2000（小霸王学习机）',
  missing: (file) => `找不到 fcdev 的文档：${file}`,
  marker: (file) => `<!-- 本文件由 npm run help:build 从 vscode/${file} 导入，不要手改 -->`
};

/** 单独成行的锚点标签，导入时原样保留。 */
const ANCHOR_LINE = /^<a id="[^"]+"><\/a>$/;
/** 导入时去掉的“目录”一节的标题行。 */
const CONTENTS_HEADING = '## 目录';
/** 行内链接：[文字](目标)。 */
const LINK = /\[([^\]]*)\]\(([^)\s]+)\)/g;
/** 带协议的外部链接。 */
const EXTERNAL = /^[a-z][a-z0-9+.-]*:/i;

/**
 * 主题所属的分组名。
 * @param {{file: string}} topic 主题登记项
 * @returns {string} 分组名
 */
export function groupTitleOf(topic) {
  if (topic.file.startsWith('nes/')) return TEXT.nes;
  if (topic.file.startsWith('sb2000/') || topic.file.startsWith('targets/sb2000/')) return TEXT.sb2000;
  return TEXT.common;
}

/**
 * 把主题按分组归类，分组顺序固定为通用、NES、SB2000，组内保持登记顺序，空分组不返回。
 * @template {{file: string}} T
 * @param {readonly T[]} topics 主题登记项
 * @returns {{title: string, topics: T[]}[]} 分组清单
 */
export function groupTopics(topics) {
  return [TEXT.common, TEXT.nes, TEXT.sb2000]
    .map((title) => ({ title, topics: topics.filter((topic) => groupTitleOf(topic) === title) }))
    .filter((group) => group.topics.length > 0);
}

/**
 * 站点侧边栏：概述加各分组。
 * @param {readonly {topic: string, title: string, file: string}[]} [topics] 主题登记项
 * @returns {{text: string, items: {text: string, link: string}[]}[]} VitePress 侧边栏分组
 */
export function sidebarGroups(topics = DOC_TOPICS) {
  return [
    { text: TEXT.title, items: [{ text: TEXT.overview, link: '/fcdev/' }] },
    ...groupTopics(topics).map((group) => ({ text: group.title, items: group.topics.map((topic) => ({ text: topic.title, link: `/fcdev/${topic.topic}` })) }))
  ];
}

/**
 * 主题的源文件绝对路径。
 * @param {{file: string, root?: 'extension'}} topic 主题登记项；root 为 extension 时文件相对扩展目录，否则相对 docs 目录
 * @param {string} [extensionRoot] fcdev 扩展目录
 * @returns {string} 绝对路径
 */
export function topicSourceFile(topic, extensionRoot = EXTENSION_ROOT) {
  const parts = topic.file.split('/');
  return topic.root === 'extension' ? join(extensionRoot, ...parts) : join(extensionRoot, 'docs', ...parts);
}

/**
 * 改写一行里的链接：指向其他登记文档的改成站内页面，其余指向文档目录之外的改成普通文字。
 * @param {string} line 一行文字
 * @param {string} sourceFile 这一行所在文档的绝对路径
 * @param {ReadonlyMap<string, string>} pageByFile 源文件绝对路径到页面主题名的映射
 * @returns {string} 改写后的行
 */
function rewriteLinks(line, sourceFile, pageByFile) {
  return line.replace(LINK, (whole, text, target) => {
    if (target.startsWith('#') || EXTERNAL.test(target)) return whole;
    const hashAt = target.indexOf('#');
    const pathPart = hashAt < 0 ? target : target.slice(0, hashAt);
    const hash = hashAt < 0 ? '' : target.slice(hashAt);
    const page = pageByFile.get(resolve(dirname(sourceFile), decodeURI(pathPart)));
    return page === undefined ? text : `[${text}](${page}.md${hash})`;
  });
}

/**
 * 转义行里代码段之外的尖括号与双花括号，避免被 VitePress 当成 Vue 模板。
 * @param {string} line 一行文字
 * @returns {string} 转义后的行
 */
function escapeMarkup(line) {
  if (ANCHOR_LINE.test(line)) return line;
  return line
    .split(/(`[^`]*`)/)
    .map((part, index) => (index % 2 === 1 ? part : part.replaceAll('<', '&lt;').replaceAll('{{', '&#123;&#123;')))
    .join('');
}

/**
 * 把一份文档转成帮助站点的页面：统一换行，去掉单独成节的“## 目录”，代码块原样保留，其余行先改写链接再转义，开头加“由导入生成”的标记。
 * @param {object} input 参数
 * @param {string} input.text 文档全文
 * @param {string} input.sourceFile 文档的绝对路径
 * @param {string} input.relativeFile 文档相对扩展目录的路径，写进标记
 * @param {ReadonlyMap<string, string>} input.pageByFile 源文件绝对路径到页面主题名的映射
 * @returns {string} 页面文本（LF 换行）
 */
export function convertDocument({ text, sourceFile, relativeFile, pageByFile }) {
  let inFence = false;
  let inContents = false;
  const lines = [];
  for (const line of text.replace(/\r\n/g, '\n').split('\n')) {
    if (/^\s*(```|~~~)/.test(line)) {
      inContents = false;
      inFence = !inFence;
      lines.push(line);
      continue;
    }
    if (inFence) {
      lines.push(line);
      continue;
    }
    // 目录一节只有列表项与空行，遇到别的行（下一节的锚点或标题）就结束，该行要保留
    if (line === CONTENTS_HEADING) {
      inContents = true;
      continue;
    }
    if (inContents && (line === '' || line.startsWith('- '))) continue;
    inContents = false;
    lines.push(escapeMarkup(rewriteLinks(line, sourceFile, pageByFile)));
  }
  return `${TEXT.marker(relativeFile)}\n\n${lines.join('\n').replace(/\n+$/, '')}\n`;
}

/**
 * 概述页：简介加按分组列出的手册表。
 * @param {readonly {topic: string, title: string, summary: string, file: string}[]} topics 主题登记项
 * @returns {string} 页面文本
 */
export function renderOverview(topics) {
  const lines = [`# ${TEXT.title}`, '', TEXT.intro, '', TEXT.source, ''];
  for (const group of groupTopics(topics)) {
    lines.push(`## ${group.title}`, '', `| ${TEXT.columns[0]} | ${TEXT.columns[1]} |`, '| --- | --- |');
    for (const topic of group.topics) lines.push(`| [${topic.title}](${topic.topic}.md) | ${topic.summary.replace(/\|/g, '\\|')} |`);
    lines.push('');
  }
  return `<!-- 本文件由 npm run help:build 根据 fcdev 扩展的主题登记表生成，不要手改 -->\n\n${lines.map(escapeMarkup).join('\n')}`;
}

/**
 * 导入全部文档到目标目录：只写有变化的页面，删除不再登记的页面。
 * @param {object} [options] 选项
 * @param {readonly {topic: string, title: string, summary: string, file: string, root?: 'extension'}[]} [options.topics] 主题登记项
 * @param {string} [options.extensionRoot] fcdev 扩展目录
 * @param {string} [options.outputDirectory] 目标目录
 * @returns {{pages: number, written: number}} 页数与实际写入的页数
 */
export function importFcdevDocs({ topics = DOC_TOPICS, extensionRoot = EXTENSION_ROOT, outputDirectory = HELP_FCDEV_DIRECTORY } = {}) {
  const pageByFile = new Map(topics.map((topic) => [resolve(topicSourceFile(topic, extensionRoot)), topic.topic]));
  /** @type {Map<string, string>} */
  const pages = new Map([['index.md', renderOverview(topics)]]);
  for (const topic of topics) {
    const sourceFile = topicSourceFile(topic, extensionRoot);
    if (!existsSync(sourceFile)) throw new Error(TEXT.missing(sourceFile));
    const relativeFile = relative(extensionRoot, sourceFile).replaceAll('\\', '/');
    pages.set(`${topic.topic}.md`, convertDocument({ text: readFileSync(sourceFile, 'utf8'), sourceFile: resolve(sourceFile), relativeFile, pageByFile }));
  }

  mkdirSync(outputDirectory, { recursive: true });
  for (const name of readdirSync(outputDirectory)) if (name.endsWith('.md') && !pages.has(name)) rmSync(join(outputDirectory, name));
  let written = 0;
  for (const [name, content] of pages) {
    const file = join(outputDirectory, name);
    if (existsSync(file) && readFileSync(file, 'utf8') === content) continue;
    writeFileSync(file, content, 'utf8');
    written += 1;
  }
  return { pages: pages.size, written };
}

if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  try {
    const { pages, written } = importFcdevDocs();
    console.log(`fcdev 手册：${pages} 页，写入 ${written} 页：ember/help/fcdev`);
  } catch (error) {
    console.error(error instanceof Error ? error.message : String(error));
    process.exit(1);
  }
}
