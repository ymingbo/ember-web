/*
 * ------------------------------------------------------------------------
 * 名称：install-build-tools.mjs
 * 说明：安装重新编译 FFmpeg 所需的工具，只供维护者手动运行（npm run ffmpeg:tools）：用 winget 安装 MSYS2，再用 pacman 安装 make、diffutils。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-20
 * 备注：会联网下载并改动系统，所以不接入构建；已安装的工具跳过；还需要带 C++ 工具的 Visual Studio。
 * ------------------------------------------------------------------------
 */

import { execFileSync } from 'node:child_process';
import { existsSync } from 'node:fs';
import { msys2Bash, MSYS2_PACKAGES } from './msys2.mjs';

/** 运行命令，输出直接显示在终端。 */
function run(file, args, env) {
  execFileSync(file, args, { stdio: 'inherit', env: env ?? process.env });
}

if (process.platform !== 'win32') throw new Error('只支持在 Windows 上编译 FFmpeg');
const bash = msys2Bash();
if (!existsSync(bash)) {
  console.log('安装 MSYS2……');
  run('winget', ['install', '--id', 'MSYS2.MSYS2', '--exact', '--silent', '--accept-package-agreements', '--accept-source-agreements']);
  if (!existsSync(bash)) throw new Error(`安装后仍找不到 ${bash}；MSYS2 不在默认位置时，用环境变量 EMBER_MSYS2 指定它的安装目录`);
}
const env = { ...process.env, MSYSTEM: 'MSYS', CHERE_INVOKING: '1' };
console.log(`安装 ${MSYS2_PACKAGES.join('、')}……`);
run(bash, ['-lc', `pacman -S --needed --noconfirm ${MSYS2_PACKAGES.join(' ')}`], env);
console.log('FFmpeg 的编译工具已就绪。');
