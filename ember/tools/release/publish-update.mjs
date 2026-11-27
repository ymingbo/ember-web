/*
 * ------------------------------------------------------------------------
 * 名称：publish-update.mjs
 * 说明：把升级文件发布到阿里云 OSS 的升级目录（npm run update:publish）：用私钥给 build/ember/release/Ember-<版本号>.exe 生成签名清单 latest.json，再先传程序文件、最后传 latest.json。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-21
 * 备注：先 npm run package:win 打好发布包，并把 package.json 的 version 升到新版本；上传用阿里云官方命令行工具 ossutil（要先 ossutil config 配好访问密钥，密钥不放进仓库）；--dry-run 只检查并写出 latest.json，不上传；
 *       位置不变：Bucket famiclone 的 updates/ember/windows/（旧版本留下的 RELEASES 与 .nupkg 不动），程序里的升级地址是 https://famiclone.oss-cn-beijing.aliyuncs.com/updates/ember/windows；
 *       客户端按 latest.json 找新版本，必须在程序文件传好之后才让它对外可见，所以最后上传它；程序文件名带版本号，新版本不会覆盖以前传的。
 *       发布前会核对：程序里编译进去的公钥必须就是这把私钥的公钥，否则发布出去的升级没有一个已装程序能接受；签名生成后还会用公钥再校验一遍。
 *       可作为模块被测试引用（buildLatest、planUploads、publicKeyMatches），也可直接运行。
 * ------------------------------------------------------------------------
 */

import { spawnSync } from 'node:child_process';
import { createPublicKey, createVerify } from 'node:crypto';
import { existsSync, readFileSync, writeFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { RELEASE_BUILD_DIRECTORY } from '../build/build-paths.mjs';
import { PUBLIC_KEY_HEADER, buildManifest, formatPublicKeyHeader, loadPrivateKey, publicKeyParts, signedMessage } from './update-signing.mjs';

/** 存放升级文件的 OSS Bucket。 */
const BUCKET = 'famiclone';
/** 升级文件在 Bucket 里的目录，与程序里的升级地址对应。 */
const KEY_PREFIX = 'updates/ember/windows';
/** 清单文件名。 */
export const LATEST_FILE = 'latest.json';

/**
 * 生成清单并签名，写成 latest.json 的内容。
 * @param {import('node:crypto').KeyObject} privateKey 签名私钥。
 * @param {string} version 版本号。
 * @param {Buffer} content 程序文件的内容。
 */
export function buildLatest(privateKey, version, content) {
  if (!/^\d+\.\d+\.\d+$/.test(version)) throw new Error(`版本号必须是“主.次.修订”：${version}`);
  const manifest = buildManifest(privateKey, { version, content });
  const verified = createVerify('sha256').update(signedMessage(manifest)).verify(createPublicKey(privateKey), Buffer.from(manifest.signature, 'base64'));
  if (!verified) throw new Error('签名生成后用公钥校验没有通过');
  return `${JSON.stringify(manifest, null, 2)}\n`;
}

/** 要上传的文件与它们在 Bucket 里的路径：程序文件在前，清单在最后。 */
export function planUploads(version) {
  const file = `Ember-${version}.exe`;
  return [
    { file, key: `${KEY_PREFIX}/${file}` },
    { file: LATEST_FILE, key: `${KEY_PREFIX}/${LATEST_FILE}` }
  ];
}

/** 程序里编译进去的公钥头文件是否就是这把私钥的公钥。 */
export function publicKeyMatches(privateKey, headerText) {
  return headerText.replaceAll('\r\n', '\n') === formatPublicKeyHeader(publicKeyParts(privateKey));
}

/** 命令行：检查、签名并上传。 */
function main() {
  const root = resolve(dirname(fileURLToPath(import.meta.url)), '..', '..');
  const dryRun = process.argv.includes('--dry-run');
  const { version } = JSON.parse(readFileSync(join(root, 'package.json'), 'utf8'));
  const exe = join(RELEASE_BUILD_DIRECTORY, `Ember-${version}.exe`);
  if (!existsSync(exe)) throw new Error(`找不到 ${exe}：先运行 npm run package:win（并确认 package.json 的 version 就是要发布的版本）`);
  const privateKey = loadPrivateKey();
  if (!publicKeyMatches(privateKey, readFileSync(PUBLIC_KEY_HEADER, 'utf8'))) {
    throw new Error(`${PUBLIC_KEY_HEADER} 里的公钥不是这把私钥的公钥：重新运行 npm run update:keygen -- --export-public 并重新打包`);
  }
  const latest = join(RELEASE_BUILD_DIRECTORY, LATEST_FILE);
  writeFileSync(latest, buildLatest(privateKey, version, readFileSync(exe)));
  const uploads = planUploads(version);
  console.log(`版本 ${version}：签名清单已写到 ${latest}，将上传 ${uploads.length} 个文件到 oss://${BUCKET}/${KEY_PREFIX}/`);
  for (const upload of uploads) {
    const target = `oss://${BUCKET}/${upload.key}`;
    console.log(`${dryRun ? '（仅检查）' : '上传'} ${upload.file} -> ${target}`);
    if (dryRun) continue;
    const result = spawnSync('ossutil', ['cp', '-f', join(RELEASE_BUILD_DIRECTORY, upload.file), target], { stdio: 'inherit', shell: process.platform === 'win32' });
    if (result.error !== undefined || result.status !== 0) throw new Error(`上传 ${upload.file} 失败：先确认已安装 ossutil 并用 ossutil config 配好访问密钥`);
  }
  console.log(dryRun ? '检查完成，没有上传。' : '发布完成。');
}

if (import.meta.url === pathToFileURL(process.argv[1] ?? '').href) {
  try {
    main();
  } catch (error) {
    console.error(error instanceof Error ? error.message : String(error));
    process.exit(1);
  }
}
