/*
 * ------------------------------------------------------------------------
 * 名称：gen-screen-shaders.mjs
 * 说明：把运行画面的 HLSL（win32/app/src/platform/display/screen.hlsl）用 fxc 编译成字节码，写成 screen-shaders.generated.cpp。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-19
 * 备注：只在改了 screen.hlsl 时手动运行（npm run win32:shaders）；生成结果提交进仓库，所以构建与运行都不需要 fxc 与 d3dcompiler_47.dll；
 *       fxc 取本机 Windows SDK（C:\Program Files (x86)\Windows Kits\10\bin\<版本>\x64\fxc.exe）里最新的一个。
 * ------------------------------------------------------------------------
 */

import { execFileSync } from 'node:child_process';
import { existsSync, mkdtempSync, readdirSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const EMBER_ROOT = join(dirname(fileURLToPath(import.meta.url)), '..', '..');
const DISPLAY_DIRECTORY = join(EMBER_ROOT, 'win32', 'app', 'src', 'platform', 'display');
const SOURCE = join(DISPLAY_DIRECTORY, 'screen.hlsl');
const OUTPUT = join(DISPLAY_DIRECTORY, 'screen-shaders.generated.cpp');
const SDK_BIN = 'C:\\Program Files (x86)\\Windows Kits\\10\\bin';

/** 着色器的入口、档次与变量名。 */
const SHADERS = [
  { entry: 'vertexMain', profile: 'vs_4_0_level_9_1', name: 'kScreenVertexShader' },
  { entry: 'pixelMain', profile: 'ps_4_0_level_9_1', name: 'kScreenPixelShader' },
  { entry: 'glassMain', profile: 'ps_4_0_level_9_1', name: 'kScreenGlassShader' }
];

/** 找到本机 Windows SDK 里最新的 fxc.exe。 */
function findFxc() {
  if (!existsSync(SDK_BIN)) throw new Error(`找不到 Windows SDK：${SDK_BIN}`);
  const versions = readdirSync(SDK_BIN)
    .filter((name) => /^10\.\d+\.\d+\.\d+$/.test(name))
    .toSorted((left, right) => right.localeCompare(left, undefined, { numeric: true }));
  for (const version of versions) {
    const path = join(SDK_BIN, version, 'x64', 'fxc.exe');
    if (existsSync(path)) return path;
  }
  throw new Error('Windows SDK 里没有 fxc.exe');
}

/** 字节写成每行 16 个的十六进制数组。 */
function formatBytes(bytes) {
  const lines = [];
  for (let offset = 0; offset < bytes.length; offset += 16) {
    lines.push(`    ${Array.from(bytes.subarray(offset, offset + 16), (value) => `0x${value.toString(16).padStart(2, '0')}`).join(', ')}`);
  }
  return lines.join(',\n');
}

const fxc = findFxc();
const directory = mkdtempSync(join(tmpdir(), 'ember-shaders-'));
try {
  const blocks = SHADERS.map((shader) => {
    const output = join(directory, `${shader.name}.cso`);
    execFileSync(fxc, ['/nologo', '/O3', '/T', shader.profile, '/E', shader.entry, '/Fo', output, SOURCE], { stdio: 'inherit' });
    const bytes = readFileSync(output);
    return `/** ${shader.profile}，入口 ${shader.entry}。 */\nconst unsigned char ${shader.name}[] = {\n${formatBytes(bytes)}};\nconst std::size_t ${shader.name}Size = sizeof(${shader.name});`;
  });
  const text = `// 由 ember/tools/build/gen-screen-shaders.mjs 根据 screen.hlsl 生成，不要手改。

#include <cstddef>

#include "platform/display/screen-shaders.hpp"

namespace ember::win32::platform {

${blocks.join('\n\n')}

}  // namespace ember::win32::platform
`;
  writeFileSync(OUTPUT, text, 'utf8');
  console.log(`已生成 ${OUTPUT}`);
} finally {
  rmSync(directory, { recursive: true, force: true });
}
