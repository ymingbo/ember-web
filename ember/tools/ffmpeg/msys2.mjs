/*
 * ------------------------------------------------------------------------
 * 名称：msys2.mjs
 * 说明：编译 FFmpeg 用的 MSYS2：安装位置与需要的软件包。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-20
 * 备注：默认安装在 C:\msys64，可用环境变量 EMBER_MSYS2 指定其他位置。
 * ------------------------------------------------------------------------
 */

import { join } from 'node:path';

/** FFmpeg 的 configure 与 make 用到的软件包。 */
export const MSYS2_PACKAGES = ['make', 'diffutils'];

/** MSYS2 的 bash。 */
export function msys2Bash() {
  return join(process.env.EMBER_MSYS2 ?? 'C:\\msys64', 'usr', 'bin', 'bash.exe');
}
