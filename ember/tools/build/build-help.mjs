/*
 * ------------------------------------------------------------------------
 * 名称：build-help.mjs
 * 说明：构建 Ember 的帮助文档站点：先由 VS Code 扩展里的生成脚本把 OpenRPC 接口文档生成成 help/api 下的各模块页，再把 fcdev 扩展的使用文档导入 help/fcdev，最后用 VitePress 把 help/ 构建成静态站点，输出到 build/ember/help。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-18
 * 备注：要先运行过 npm run schema（vscode/debug/openrpc.json 由它生成），并在 vscode 目录运行过 npm run build（接口文档的生成器是扩展的编译产物）；
 *       扩展的 C 接口参考、符号参考与示例程序源码由仓库根目录 tools 下的生成脚本先重新生成（没有变化时文件不变），再随手册一起导入，help/fcdev 不提交；
 *       npm run package:win 与 npm run check 会先运行本脚本；站点由 resources:pack 打进资源包，嵌进可执行文件。
 * ------------------------------------------------------------------------
 */

import { execFileSync } from 'node:child_process';
import { existsSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { build } from 'vitepress';
import { HELP_BUILD_DIRECTORY } from './build-paths.mjs';
import { importFcdevDocs } from './import-fcdev-docs.mjs';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '..', '..');
const repoRoot = join(root, '..');
const extension = join(root, '..', 'vscode');
const helpRoot = join(root, 'help');

/** 先重新生成的 fcdev 扩展文档：C 接口参考、符号参考与示例程序源码。 */
const FCDEV_GENERATORS = ['gen-sb2000-api-doc.mjs', 'gen-sb2000-symbols-doc.mjs', 'gen-sb2000-examples-doc.mjs'];

/** 用户可见的文字。 */
const TEXT = {
  noExtension: `找不到 VS Code 扩展：${extension}`,
  noOpenRpc: '扩展里没有 debug/openrpc.json：先运行 npm run schema',
  noBuild: '找不到扩展的编译产物：先在 vscode 目录运行 npm run build',
  noGenerator: (script) => `找不到 fcdev 文档的生成脚本：${join(extension, 'tools', 'gen', script)}`
};

try {
  if (!existsSync(extension)) throw new Error(TEXT.noExtension);
  if (!existsSync(join(extension, 'debug', 'openrpc.json'))) throw new Error(TEXT.noOpenRpc);
  if (!existsSync(join(extension, 'out', 'shared', 'debug-client', 'api-reference.js'))) throw new Error(TEXT.noBuild);

  // 调试接口的各模块页：由接口定义生成，写到 help/api（不提交）。
  execFileSync(process.execPath, [join(extension, 'tools', 'gen', 'gen-debug-api-docs.mjs'), join(helpRoot, 'api')], { cwd: extension, stdio: 'inherit' });

  // fcdev 扩展的手册：参考文档先重新生成，再连同手册一起导入 help/fcdev（不提交）。
  for (const script of FCDEV_GENERATORS) {
    if (!existsSync(join(extension, 'tools', 'gen', script))) throw new Error(TEXT.noGenerator(script));
    execFileSync(process.execPath, [join(extension, 'tools', 'gen', script)], { cwd: repoRoot, stdio: 'inherit' });
  }
  const { pages, written } = importFcdevDocs();
  console.log(`fcdev 手册：${pages} 页，写入 ${written} 页：ember/help/fcdev`);
  await build(helpRoot);
  console.log(`帮助文档：已生成 ${HELP_BUILD_DIRECTORY}`);
} catch (error) {
  console.error(error instanceof Error ? error.message : String(error));
  process.exit(1);
}
