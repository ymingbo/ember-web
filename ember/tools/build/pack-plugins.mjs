/*
 * ------------------------------------------------------------------------
 * 名称：pack-plugins.mjs
 * 说明：把 plugins/<插件>/ 目录打成 build/ember/plugins/<id>.emap（zip，根目录只含 manifest.json、main.js、icon.png），供开发版预装与 C++ 测试使用。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * 备注：只用 Node 自带的 zlib，不引入依赖；输出是确定的（固定时间戳、按文件名排序），同样的输入得到同样的包。
 *       这里只检查打包需要的内容（清单是 JSON、有合规的 id、目录名等于 id、没有多余的文件）；完整的清单与脚本检查在核心安装时做。
 *       包格式见 docs/03-mapper插件规范.md 第 3 节。可作为模块被测试引用（packPlugins），也可直接运行。
 * ------------------------------------------------------------------------
 */

import { existsSync, mkdirSync, readdirSync, readFileSync, statSync, writeFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { deflateRawSync } from 'node:zlib';
import { PLUGINS_BUILD_DIRECTORY } from './build-paths.mjs';

/** 包里允许的文件名，与核心的检查一致。 */
const PACKAGE_FILES = ['manifest.json', 'main.js', 'icon.png'];
const ID_PATTERN = /^[a-z0-9][a-z0-9-]{2,39}$/;
/** zip 里固定的 DOS 时间戳：1980-01-01 00:00:00，保证输出可重现。 */
const DOS_TIME = 0;
const DOS_DATE = (0 << 9) | (1 << 5) | 1;
/** 通用标志位 11：文件名是 UTF-8。 */
const FLAG_UTF8 = 0x0800;
/**
 * 附在包末尾的可压缩注释（256 个空格）。压不小的文件会被压缩工具原样存放，而原样存放的 zip 嵌在别的 zip 里时，外层的解压会在它之后停止，
 * 后面的文件全部丢失；有这段注释，整个包就总能压得更小，会被压缩存放。
 */
const COMPRESSIBLE_COMMENT = Buffer.alloc(256, 0x20);

/** CRC-32 查表。 */
const CRC_TABLE = (() => {
  const table = new Uint32Array(256);
  for (let index = 0; index < 256; index += 1) {
    let value = index;
    for (let bit = 0; bit < 8; bit += 1) value = value & 1 ? (value >>> 1) ^ 0xedb88320 : value >>> 1;
    table[index] = value >>> 0;
  }
  return table;
})();

/** 计算 CRC-32。 */
function crc32(data) {
  let crc = 0xffffffff;
  for (const byte of data) crc = CRC_TABLE[(crc ^ byte) & 0xff] ^ (crc >>> 8);
  return (crc ^ 0xffffffff) >>> 0;
}

/**
 * 生成 zip：每个文件一个条目，能压小就用 deflate，否则存储。
 * @param {Array<{ name: string, data: Buffer }>} entries 文件，按给定顺序写入。
 * @param {Buffer} [comment] 写在末尾的注释，默认没有。
 * @returns {Buffer} zip 文件内容。
 */
export function createZip(entries, comment = Buffer.alloc(0)) {
  const locals = [];
  const centrals = [];
  let offset = 0;
  for (const { name, data } of entries) {
    const nameBytes = Buffer.from(name, 'utf8');
    const deflated = deflateRawSync(data, { level: 9 });
    const useDeflate = deflated.length < data.length;
    const body = useDeflate ? deflated : data;
    const crc = crc32(data);
    const local = Buffer.alloc(30);
    local.writeUInt32LE(0x04034b50, 0);
    local.writeUInt16LE(20, 4);
    local.writeUInt16LE(FLAG_UTF8, 6);
    local.writeUInt16LE(useDeflate ? 8 : 0, 8);
    local.writeUInt16LE(DOS_TIME, 10);
    local.writeUInt16LE(DOS_DATE, 12);
    local.writeUInt32LE(crc, 14);
    local.writeUInt32LE(body.length, 18);
    local.writeUInt32LE(data.length, 22);
    local.writeUInt16LE(nameBytes.length, 26);
    local.writeUInt16LE(0, 28);
    locals.push(local, nameBytes, body);

    const central = Buffer.alloc(46);
    central.writeUInt32LE(0x02014b50, 0);
    central.writeUInt16LE(20, 4);
    central.writeUInt16LE(20, 6);
    central.writeUInt16LE(FLAG_UTF8, 8);
    central.writeUInt16LE(useDeflate ? 8 : 0, 10);
    central.writeUInt16LE(DOS_TIME, 12);
    central.writeUInt16LE(DOS_DATE, 14);
    central.writeUInt32LE(crc, 16);
    central.writeUInt32LE(body.length, 20);
    central.writeUInt32LE(data.length, 24);
    central.writeUInt16LE(nameBytes.length, 28);
    central.writeUInt32LE(offset, 42);
    centrals.push(central, nameBytes);
    offset += local.length + nameBytes.length + body.length;
  }
  const directory = Buffer.concat(centrals);
  const end = Buffer.alloc(22);
  end.writeUInt32LE(0x06054b50, 0);
  end.writeUInt16LE(entries.length, 8);
  end.writeUInt16LE(entries.length, 10);
  end.writeUInt32LE(directory.length, 12);
  end.writeUInt32LE(offset, 16);
  end.writeUInt16LE(comment.length, 20);
  return Buffer.concat([...locals, directory, end, comment]);
}

/**
 * 打包一个插件目录。
 * @param {string} sourceDir 插件目录，名称必须等于清单里的 id。
 * @param {string} outDir 输出目录。
 * @returns {{ id: string, file: string }} 插件标识与输出的 .emap 路径。
 */
export function packPlugin(sourceDir, outDir) {
  const directoryName = sourceDir.split(/[\\/]/).filter(Boolean).pop();
  const files = readdirSync(sourceDir).toSorted();
  for (const name of files) {
    if (!PACKAGE_FILES.includes(name) || !statSync(join(sourceDir, name)).isFile()) throw new Error(`${directoryName}：插件目录里不允许出现 ${name}（只允许 ${PACKAGE_FILES.join('、')}）`);
  }
  if (!files.includes('manifest.json')) throw new Error(`${directoryName}：缺少 manifest.json`);
  let manifest;
  try {
    manifest = JSON.parse(readFileSync(join(sourceDir, 'manifest.json'), 'utf8'));
  } catch (error) {
    throw new Error(`${directoryName}：manifest.json 不是合法的 JSON（${error instanceof Error ? error.message : String(error)}）`, { cause: error });
  }
  if (typeof manifest?.id !== 'string' || !ID_PATTERN.test(manifest.id)) throw new Error(`${directoryName}：manifest.json 的 id 必须是 3 到 40 个小写字母、数字或 -`);
  if (manifest.id !== directoryName) throw new Error(`${directoryName}：目录名必须等于清单里的 id（${manifest.id}）`);
  const entries = files.map((name) => ({ name, data: readFileSync(join(sourceDir, name)) }));
  mkdirSync(outDir, { recursive: true });
  const file = join(outDir, `${manifest.id}.emap`);
  writeFileSync(file, createZip(entries, COMPRESSIBLE_COMMENT));
  return { id: manifest.id, file };
}

/**
 * 打包 plugins/ 下的全部插件，默认输出到 build/ember/plugins/。
 * @param {string} root 项目根目录。
 * @param {string} [outDir] 输出目录，测试里用临时目录。
 * @returns {Array<{ id: string, file: string }>} 打出的插件包。
 */
export function packPlugins(root, outDir = PLUGINS_BUILD_DIRECTORY) {
  const pluginsDir = join(root, 'plugins');
  if (!existsSync(pluginsDir)) return [];
  return readdirSync(pluginsDir)
    .toSorted()
    .filter((name) => statSync(join(pluginsDir, name)).isDirectory())
    .map((name) => packPlugin(join(pluginsDir, name), outDir));
}

if (import.meta.url === pathToFileURL(process.argv[1] ?? '').href) {
  const root = resolve(dirname(fileURLToPath(import.meta.url)), '..', '..');
  try {
    const packed = packPlugins(root);
    console.log(`插件打包：${packed.length} 个（${packed.map((item) => item.id).join('、')}）→ build/ember/plugins`);
  } catch (error) {
    console.error(error instanceof Error ? error.message : String(error));
    process.exit(1);
  }
}
