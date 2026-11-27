/*
 * ------------------------------------------------------------------------
 * 名称：check-win32-ui-kit.mjs
 * 说明：Win32 控件库的结构检查：#include 的依赖方向、公共头文件不暴露系统图形头、控件不直接引用系统图形接口、控件之间不互相引用、源文件都登记到 CMake，并检查生成的令牌、图标与字体文件与生成结果一致。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-19
 * 备注：规则：
 *       1. include/ 里的公共头文件只能引用 include/ 和标准库，不引用 src/ 与 Windows、Direct2D、DirectWrite、WIC 的头文件；
 *       2. src/base 不引用 src/components；
 *       3. src/components/<控件> 只能引用公共头文件、base/theme-tokens.hpp 与自己目录里的文件，不引用别的控件，也不引用系统图形头文件（控件只经 Canvas 绘制）；
 *       4. src 下的每个 .cpp 都在 ui-kit/CMakeLists.txt 的库目标里登记；
 *       5. 令牌、图标与字体的生成文件与生成结果一致。
 *       checkUiKit(root) 可被测试引用，对任意目录做规则 1 到 4 的检查。
 * ------------------------------------------------------------------------
 */

import { existsSync, readdirSync, readFileSync, statSync } from 'node:fs';
import { dirname, join, posix, relative, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

/** 系统图形与窗口头文件：只有 src/base 可以引用。 */
const SYSTEM_GRAPHICS_HEADERS = /^(windows\.h|windowsx\.h|d2d1(_\d)?\.h|dwrite(_\d)?\.h|wincodec\.h|dxgi\w*\.h|d3d\w+\.h|wrl\/.*)$/;

/** 控件可以引用的 base 里的文件：令牌入口、按文字匹配图标的规则、UTF-8 与 UTF-16 转换。 */
const ALLOWED_BASE_HEADERS = new Set(['base/theme-tokens.hpp', 'base/icon-rules.hpp', 'base/unicode.hpp']);

/**
 * 递归列出目录里的文件。
 * @param {string} directory 目录。
 * @returns {string[]} 文件的绝对路径。
 */
function listFiles(directory) {
  if (!existsSync(directory)) return [];
  const files = [];
  for (const name of readdirSync(directory)) {
    const path = join(directory, name);
    if (statSync(path).isDirectory()) files.push(...listFiles(path));
    else if (/\.(cpp|hpp|h)$/.test(name)) files.push(path);
  }
  return files;
}

/**
 * 读出文件里的 #include（引号与尖括号两种）。
 * @param {string} file 文件。
 * @returns {{ name: string, system: boolean }[]} 引用列表。
 */
function readIncludes(file) {
  const includes = [];
  for (const line of readFileSync(file, 'utf8').split(/\r?\n/)) {
    const match = /^\s*#\s*include\s+([<"])([^>"]+)[>"]/.exec(line);
    if (match) includes.push({ name: match[2], system: match[1] === '<' });
  }
  return includes;
}

/**
 * 检查控件库目录。
 * @param {string} root 控件库根目录（含 include、src、CMakeLists.txt）。
 * @returns {string[]} 违规说明，没有违规时为空数组。
 */
export function checkUiKit(root) {
  const problems = [];
  const toRelative = (file) => relative(root, file).split('\\').join('/');

  for (const file of listFiles(join(root, 'include'))) {
    for (const include of readIncludes(file)) {
      if (include.system ? SYSTEM_GRAPHICS_HEADERS.test(include.name) : !include.name.startsWith('rujian/ui/')) problems.push(`${toRelative(file)}：公共头文件不能引用 ${include.name}`);
    }
  }

  for (const file of listFiles(join(root, 'src'))) {
    const path = toRelative(file);
    const parts = path.split('/');
    const area = parts[1];
    for (const include of readIncludes(file)) {
      const name = include.name;
      if (area === 'base' && !include.system && name.startsWith('components/')) problems.push(`${path}：base 不能引用控件 ${name}`);
      if (area !== 'components') continue;
      const own = parts[2];
      if (include.system) {
        if (SYSTEM_GRAPHICS_HEADERS.test(name)) problems.push(`${path}：控件不能直接引用系统头文件 <${name}>，只经 Canvas 绘制`);
        continue;
      }
      const allowed = name.startsWith('rujian/ui/') || ALLOWED_BASE_HEADERS.has(name) || (name.startsWith('components/') && posix.dirname(name).split('/')[1] === own);
      if (!allowed) problems.push(`${path}：控件只能引用公共头文件、base/theme-tokens.hpp、base/icon-rules.hpp、base/unicode.hpp 和自己目录里的文件，不能引用 ${name}`);
    }
  }

  const cmakeFile = join(root, 'CMakeLists.txt');
  const cmake = existsSync(cmakeFile) ? readFileSync(cmakeFile, 'utf8') : '';
  for (const file of listFiles(join(root, 'src')).filter((item) => item.endsWith('.cpp'))) {
    if (!cmake.includes(toRelative(file))) problems.push(`${toRelative(file)}：没有在 CMakeLists.txt 登记`);
  }
  return problems;
}

/**
 * 检查生成文件与生成结果一致。
 * @returns {Promise<string[]>} 不一致的说明。
 */
async function checkGenerated() {
  const problems = [];
  const tokens = await import('../../win32/ui-kit/tools/gen-tokens.mjs');
  const icons = await import('../../win32/ui-kit/tools/gen-icons.mjs');
  const fonts = await import('../../win32/ui-kit/tools/gen-fonts.mjs');
  if (!existsSync(tokens.OUTPUT_FILE) || readFileSync(tokens.OUTPUT_FILE, 'utf8') !== tokens.generate()) problems.push('tokens.generated.hpp 与生成结果不一致，运行 node win32/ui-kit/tools/gen-tokens.mjs');
  const { id, data, rules } = icons.generate();
  const iconFiles = [[icons.ID_FILE, id], [icons.DATA_FILE, data], [icons.RULES_FILE, rules]];
  if (iconFiles.some(([file, text]) => !existsSync(file) || readFileSync(file, 'utf8') !== text)) problems.push('图标文件与生成结果不一致，运行 node win32/ui-kit/tools/gen-icons.mjs');
  if (!existsSync(fonts.FONT_DATA_FILE) || readFileSync(fonts.FONT_DATA_FILE, 'utf8') !== fonts.generate()) problems.push('字体文件与生成结果不一致，运行 node win32/ui-kit/tools/gen-fonts.mjs');
  return problems;
}

if (process.argv[1] === fileURLToPath(import.meta.url)) {
  const root = resolve(dirname(fileURLToPath(import.meta.url)), '..', '..', 'win32', 'ui-kit');
  const problems = [...checkUiKit(root), ...(await checkGenerated())];
  if (problems.length > 0) {
    for (const problem of problems) console.error(problem);
    process.exit(1);
  }
  console.log('Win32 控件库结构检查通过');
}
