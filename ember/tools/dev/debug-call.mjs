/*
 * ------------------------------------------------------------------------
 * 名称：debug-call.mjs
 * 说明：开发脚本用的调试请求客户端：连接指定用户数据目录里的 Ember，发一个调试请求，把结果按一行 JSON 打印到标准输出。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-22
 * 备注：客户端在 VS Code 扩展里（vscode/），这里直接引用扩展的编译产物 vscode/out，所以要先在 vscode 目录运行 npm run build；
 *       用法：node debug-call.mjs <用户数据目录> <请求名> [key=value ...]，参数写法与扩展命令“fcdev：调用调试请求”相同（值按 JSON 解析，0x8000 与 $8000 是十六进制数）；
 *       结果里的二进制数据换成“<二进制 N 字节>”；出错时把说明写到标准错误并以退出码 1 结束。
 * ------------------------------------------------------------------------
 */

import { existsSync } from 'node:fs';
import { createRequire } from 'node:module';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const require = createRequire(import.meta.url);
const out = resolve(dirname(fileURLToPath(import.meta.url)), '..', '..', '..', 'vscode', 'out');

try {
  if (!existsSync(join(out, 'logic', 'debug-client', 'ember-connector.js'))) throw new Error('找不到扩展的编译产物，先在 vscode 目录运行 npm run build');
  const { EmberConnector } = require(join(out, 'logic', 'debug-client', 'ember-connector.js'));
  const { openWebSocket } = require(join(out, 'infra', 'debug-client', 'web-socket-transport.js'));
  const { FileEndpointResolver } = require(join(out, 'infra', 'debug-client', 'endpoint-file.js'));
  const { parseCallParams, printable } = require(join(out, 'shared', 'debug-client', 'call-arguments.js'));

  const [userDataDirectory, method, ...tokens] = process.argv.slice(2);
  if (!userDataDirectory || !method) throw new Error('用法：node debug-call.mjs <用户数据目录> <请求名> [key=value ...]');
  const client = await new EmberConnector(openWebSocket, new FileEndpointResolver()).connectToEmber({ userDataDirectory, clientName: 'ember-dev' });
  try {
    console.log(JSON.stringify(printable(await client.request(method, parseCallParams(tokens)))));
  } finally {
    client.close();
  }
} catch (error) {
  console.error(error instanceof Error ? error.message : String(error));
  process.exit(1);
}
