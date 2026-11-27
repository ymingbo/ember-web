/*
 * ------------------------------------------------------------------------
 * 名称：build-ffmpeg.mjs
 * 说明：从 core/third_party/ffmpeg/source 编译 FFmpeg 的静态库，输出到 core/third_party/ffmpeg/lib/win32-x64 与 include，
 *       只供维护者在源码或编译配置改动后手动运行（npm run ffmpeg:build），编完提交新库。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-20
 * 备注：用 Visual Studio 的 MSVC 经 MSYS2 的 bash 与 make 编译；编译配置见 ffmpeg-config.mjs；构建与打包不调用本脚本。
 * ------------------------------------------------------------------------
 */

import { execFileSync } from 'node:child_process';
import { copyFileSync, cpSync, existsSync, mkdirSync, readdirSync, readFileSync, rmSync, statSync, writeFileSync } from 'node:fs';
import { availableParallelism } from 'node:os';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { findVisualStudio } from '../build/find-cmake.mjs';
import {
  FFMPEG_BUILD_DIRECTORY,
  FFMPEG_LIBRARIES,
  FFMPEG_SOURCE,
  FFMPEG_STAMP_FILE,
  FFMPEG_TARGET,
  ffmpegConfigureArgs,
  ffmpegIncludeDirectory,
  ffmpegLibraryDirectory
} from './ffmpeg-config.mjs';
import { msys2Bash } from './msys2.mjs';

const SCRIPT = join(dirname(fileURLToPath(import.meta.url)), 'build-ffmpeg.sh');
const TOOLS_HINT = '请先运行 npm run ffmpeg:tools 安装编译工具';

/** 运行命令，输出直接显示在终端。 */
function run(file, args, options = {}) {
  execFileSync(file, args, { stdio: 'inherit', ...options });
}

/** 执行 vcvars64.bat 后的环境变量。 */
function visualStudioEnvironment() {
  const installation = findVisualStudio();
  const vcvars = installation ? join(installation, 'VC', 'Auxiliary', 'Build', 'vcvars64.bat') : '';
  if (!vcvars || !existsSync(vcvars)) throw new Error('没有找到带 C++ 工具的 Visual Studio（vcvars64.bat）');
  const output = execFileSync('cmd.exe', [`/d /s /c ""${vcvars}" >nul && set"`], { encoding: 'utf8', windowsVerbatimArguments: true });
  const env = {};
  for (const line of output.split(/\r?\n/)) {
    const index = line.indexOf('=');
    if (index > 0) env[line.slice(0, index)] = line.slice(index + 1);
  }
  if (!env.VCToolsInstallDir) throw new Error('vcvars64.bat 没有设置 VCToolsInstallDir');
  return env;
}

if (process.platform !== 'win32') throw new Error('只支持在 Windows 上编译 FFmpeg');
const bash = msys2Bash();
if (!existsSync(bash)) throw new Error(`没有找到 MSYS2（${bash}），${TOOLS_HINT}`);
const env = visualStudioEnvironment();
const configureArgs = ffmpegConfigureArgs();
const buildDirectory = join(FFMPEG_BUILD_DIRECTORY, FFMPEG_TARGET);
const installDirectory = join(buildDirectory, 'install');
const objectDirectory = join(buildDirectory, 'objects');
rmSync(buildDirectory, { recursive: true, force: true });
mkdirSync(objectDirectory, { recursive: true });

// 以登录方式启动 bash，PATH 才包含 MSYS2 的 /usr/bin，其后接 Visual Studio 的路径（MSYS2_PATH_TYPE=inherit）。
run(bash, ['-l', SCRIPT.replaceAll('\\', '/'), FFMPEG_SOURCE, objectDirectory, installDirectory, String(availableParallelism()), ...configureArgs], {
  env: { ...env, MSYSTEM: 'MSYS', MSYS2_PATH_TYPE: 'inherit', CHERE_INVOKING: '1' }
});

const libraryOutput = ffmpegLibraryDirectory();
const includeOutput = ffmpegIncludeDirectory();
rmSync(libraryOutput, { recursive: true, force: true });
rmSync(includeOutput, { recursive: true, force: true });
mkdirSync(libraryOutput, { recursive: true });
const installedLibraries = join(installDirectory, 'lib');
for (const name of FFMPEG_LIBRARIES) {
  const file = readdirSync(installedLibraries).find((entry) => entry === `${name}.lib` || entry === `lib${name}.a`);
  if (file === undefined) throw new Error(`没有编出 ${name} 的静态库`);
  copyFileSync(join(installedLibraries, file), join(libraryOutput, `${name}.lib`));
  console.log(`${name}.lib：${(statSync(join(libraryOutput, `${name}.lib`)).size / 1024 / 1024).toFixed(2)} MB`);
}
cpSync(join(installDirectory, 'include'), includeOutput, { recursive: true });
const version = readFileSync(join(FFMPEG_SOURCE, 'RELEASE'), 'utf8').trim();
writeFileSync(join(libraryOutput, FFMPEG_STAMP_FILE), `${JSON.stringify({ version, configure: configureArgs }, null, 2)}\n`);
console.log(`已输出到 ${libraryOutput}，请运行 npm run ffmpeg:check 核对后提交。`);
