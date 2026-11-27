/*
 * ------------------------------------------------------------------------
 * 名称：ffmpeg-config.mjs
 * 说明：FFmpeg 的位置与编译配置：源码目录、编译好的静态库与头文件所在目录、启用的功能（configure 参数），供编译脚本与一致性检查共用。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-20
 * 备注：只编音频解码需要的部分（解封装、解码、重采样），静态链进核心，不带程序、滤镜、编码器、视频与网络；
 *       改动这里的参数后必须重新编译并提交库，否则 npm run ffmpeg:check 报错；步骤见 docs/06-构建与模块登记.md 第 5 节。
 * ------------------------------------------------------------------------
 */

import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..', '..');
/** FFmpeg 目录：source 为未修改的官方源码，lib 与 include 为编译好的静态库与头文件。 */
export const FFMPEG_DIRECTORY = join(ROOT, 'core', 'third_party', 'ffmpeg');
export const FFMPEG_SOURCE = join(FFMPEG_DIRECTORY, 'source');
/** 编译用的临时目录，不进版本库（放在仓库根目录的 build/ember 下）。 */
export const FFMPEG_BUILD_DIRECTORY = join(ROOT, '..', 'build', 'ember', 'ffmpeg');
/** 随核心静态链接的库。 */
export const FFMPEG_LIBRARIES = ['avformat', 'avcodec', 'swresample', 'avutil'];
/** 编译记录：记下源码版本与 configure 参数，供一致性检查核对。 */
export const FFMPEG_STAMP_FILE = 'build-info.json';
export const FFMPEG_TARGET = 'win32-x64';

/** 静态库所在目录。 */
export function ffmpegLibraryDirectory(target = FFMPEG_TARGET) {
  return join(FFMPEG_DIRECTORY, 'lib', target);
}

/** 头文件所在目录。 */
export function ffmpegIncludeDirectory() {
  return join(FFMPEG_DIRECTORY, 'include');
}

/** 启用的功能：只读本地文件，不联网，许可为 LGPL；运行库静态链接，最低系统为 Windows 7。 */
const ARGS = [
  '--toolchain=msvc',
  '--arch=x86_64',
  '--disable-everything',
  '--disable-autodetect',
  '--disable-doc',
  '--disable-debug',
  '--disable-programs',
  '--disable-network',
  '--disable-avdevice',
  '--disable-avfilter',
  '--disable-swscale',
  '--disable-x86asm',
  '--disable-shared',
  '--enable-static',
  '--enable-w32threads',
  '--enable-protocol=file',
  '--enable-demuxer=mov,matroska,mp3,wav,aac,flac,ogg',
  '--enable-decoder=aac,mp3float,mp3,flac,opus,vorbis,alac,pcm_s16le,pcm_s24le,pcm_s32le,pcm_f32le,pcm_u8',
  '--enable-parser=aac,mpegaudio,flac,opus,vorbis',
  '--extra-cflags=-MT -D_WIN32_WINNT=0x0601 -DWINVER=0x0601'
];

/**
 * configure 参数。
 * @returns {string[]} 参数数组。
 */
export function ffmpegConfigureArgs() {
  return [...ARGS];
}
