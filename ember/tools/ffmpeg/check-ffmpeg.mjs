/*
 * ------------------------------------------------------------------------
 * 名称：check-ffmpeg.mjs
 * 说明：核对仓库中编译好的 FFmpeg 静态库与源码和编译配置一致：版本等于 source/RELEASE，configure 参数等于 ffmpeg-config.mjs 中的参数，四个库与头文件都在；
 *       不一致说明改了源码或配置却没有重新编译。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-20
 * 备注：不一致时运行 npm run ffmpeg:build 重新编译并提交。
 * ------------------------------------------------------------------------
 */

import { existsSync, readFileSync } from 'node:fs';
import { join } from 'node:path';
import { FFMPEG_LIBRARIES, FFMPEG_SOURCE, FFMPEG_STAMP_FILE, ffmpegConfigureArgs, ffmpegIncludeDirectory, ffmpegLibraryDirectory } from './ffmpeg-config.mjs';

const problems = [];
const libraries = ffmpegLibraryDirectory();
for (const name of FFMPEG_LIBRARIES) {
  if (!existsSync(join(libraries, `${name}.lib`))) problems.push(`缺少 ${join(libraries, `${name}.lib`)}`);
  if (!existsSync(join(ffmpegIncludeDirectory(), `lib${name}`))) problems.push(`缺少头文件目录 lib${name}`);
}
const stampFile = join(libraries, FFMPEG_STAMP_FILE);
if (existsSync(stampFile)) {
  const stamp = JSON.parse(readFileSync(stampFile, 'utf8'));
  const version = readFileSync(join(FFMPEG_SOURCE, 'RELEASE'), 'utf8').trim();
  if (stamp.version !== version) problems.push(`库的版本 ${stamp.version} 与源码 ${version} 不一致`);
  if (stamp.configure.join('\n') !== ffmpegConfigureArgs().join('\n')) problems.push('库的编译参数与 ffmpeg-config.mjs 不一致');
} else {
  problems.push(`缺少 ${stampFile}`);
}
if (problems.length > 0) {
  console.error(`FFmpeg 的库与源码或编译配置不一致：\n${problems.map((problem) => `  ${problem}`).join('\n')}\n请运行 npm run ffmpeg:build 重新编译并提交。`);
  process.exit(1);
}
console.log('FFmpeg 的库与源码、编译配置一致。');
