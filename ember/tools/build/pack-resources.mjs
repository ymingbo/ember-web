/*
 * ------------------------------------------------------------------------
 * 名称：pack-resources.mjs
 * 说明：把随程序分发的资源打成一个 zip（build/ember/resources/resources.zip），由 CMake 作为资源嵌进 ember-win.exe：plugins/<id>.emap（预装插件）、help/…（帮助文档站点）、THIRD_PARTY_NOTICES.txt（第三方许可全文）。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-21
 * 备注：只用 Node 自带的 zlib（createZip 取自 pack-plugins.mjs）；输出是确定的（固定时间戳、按路径排序）；某一类资源的目录还没有构建出来时跳过它并在返回值里说明，不报错（开发时可以只构建一部分）。
 *       可作为模块被引用（packResources），也可直接运行。
 * ------------------------------------------------------------------------
 */

import { existsSync, mkdirSync, readdirSync, readFileSync, statSync, writeFileSync } from 'node:fs';
import { join, relative, sep } from 'node:path';
import { pathToFileURL } from 'node:url';
import { HELP_BUILD_DIRECTORY, PLUGINS_BUILD_DIRECTORY, RESOURCES_BUILD_DIRECTORY, THIRD_PARTY_BUILD_DIRECTORY } from './build-paths.mjs';
import { createZip } from './pack-plugins.mjs';

/** 资源包的文件名。 */
export const RESOURCE_ARCHIVE_FILE = 'resources.zip';
/** 许可全文在包里的名称。 */
const NOTICES_NAME = 'THIRD_PARTY_NOTICES.txt';

/** 递归列出目录里的文件。 */
function listFiles(directory) {
  return readdirSync(directory).flatMap((name) => {
    const path = join(directory, name);
    return statSync(path).isDirectory() ? listFiles(path) : [path];
  });
}

/**
 * 打包资源。
 * @param {object} [options] 各目录；默认取构建目录。
 * @returns {{ file: string, entries: number, skipped: string[] }} 输出的文件、条目数与因目录不存在而跳过的资源。
 */
export function packResources(options = {}) {
  const outDir = options.outDirectory ?? RESOURCES_BUILD_DIRECTORY;
  const pluginsDir = options.pluginsDirectory ?? PLUGINS_BUILD_DIRECTORY;
  const helpDir = options.helpDirectory ?? HELP_BUILD_DIRECTORY;
  const noticesFile = options.noticesFile ?? join(THIRD_PARTY_BUILD_DIRECTORY, NOTICES_NAME);
  const entries = [];
  const skipped = [];

  if (existsSync(pluginsDir)) {
    for (const name of readdirSync(pluginsDir).toSorted()) {
      if (name.endsWith('.emap')) entries.push({ name: `plugins/${name}`, data: readFileSync(join(pluginsDir, name)) });
    }
  } else {
    skipped.push('预装插件');
  }
  if (existsSync(helpDir)) {
    for (const file of listFiles(helpDir).toSorted()) entries.push({ name: `help/${relative(helpDir, file).split(sep).join('/')}`, data: readFileSync(file) });
  } else {
    skipped.push('帮助文档');
  }
  if (existsSync(noticesFile)) entries.push({ name: NOTICES_NAME, data: readFileSync(noticesFile) });
  else skipped.push('第三方许可全文');

  mkdirSync(outDir, { recursive: true });
  const file = join(outDir, RESOURCE_ARCHIVE_FILE);
  const content = createZip(entries);
  // 内容没有变化时不重写，避免触发不必要的重新链接
  if (!existsSync(file) || !readFileSync(file).equals(content)) writeFileSync(file, content);
  return { file, entries: entries.length, skipped };
}

if (import.meta.url === pathToFileURL(process.argv[1] ?? '').href) {
  const { file, entries, skipped } = packResources();
  console.log(`资源包：${entries} 项 → ${file}${skipped.length > 0 ? `（没有找到：${skipped.join('、')}）` : ''}`);
}
