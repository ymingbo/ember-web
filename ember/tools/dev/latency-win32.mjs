/*
 * ------------------------------------------------------------------------
 * 名称：latency-win32.mjs
 * 说明：原生外壳的画面延迟与丢帧测量脚本：启动 ember-win.exe 加载一张测试卡带，运行一段时间后正常关闭，读取退出时写出的诊断文件，打印核心帧率、画出帧率、丢弃帧数，以及核心发布到画出的延迟（平均、p95、最大）。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-19
 * 备注：用法：node tools/dev/latency-win32.mjs [--rom=相对 core/test/data 或绝对路径] [--seconds=30] [--no-build]；
 *       统计由应用在环境变量 EMBER_WIN_DIAGNOSTICS 指定文件时于退出写出（见 bootstrap/application.cpp）；口径与 latency-run.mjs 一致：
 *       “核心发布”是核心送出帧的时刻，“画出”是帧上传并提交给 Direct3D 的时刻，不含合成器与显示器的延迟；
 *       统计从应用启动算起，包含启动与加载的时间，所以不比较帧率，只比较“画出帧数 / 送来帧数”与被新帧顶掉的比例；
 *       只结束本脚本启动的进程（按 PID）；退出码 0 表示测量完成且三项指标都达标。
 * ------------------------------------------------------------------------
 */

import { spawnSync } from 'node:child_process';
import { isAbsolute, join } from 'node:path';

import { drive, parseArguments, projectRoot, readDiagnostics, removeUserData, sleep, startApp } from './win32-dev-support.mjs';

/** 默认卡带：1-len_ctr。 */
const DEFAULT_ROM = 'apu-test/1-len_ctr.nes';
/** 判断“没有明显丢帧”：被新帧顶掉的帧不超过送来帧数的这个比例。 */
const DROP_RATIO_LIMIT = 0.01;
/** 判断“画出的帧够多”：画出帧数不低于送来帧数的这个比例。 */
const RENDER_RATIO_LIMIT = 0.98;
/** 判断“延迟可接受”：核心发布到画出的 p95 不超过两帧的时间（毫秒）。 */
const LATENCY_LIMIT_MS = 2 * (1000 / 60.0988);
/** 关闭窗口后进程应在这个时间内退出（毫秒）。 */
const EXIT_LIMIT_MS = 6000;

const options = parseArguments(process.argv.slice(2));
const seconds = Number(options.get('seconds') ?? 30);
const romOption = options.get('rom') ?? DEFAULT_ROM;
const romPath = isAbsolute(romOption) ? romOption : join(projectRoot, 'core', 'test', 'data', romOption);

/** 毫秒数保留两位小数。 */
const ms = (value) => `${value.toFixed(2)} 毫秒`;

if (!options.has('no-build')) {
  const run = spawnSync('npm.cmd', ['run', 'win32:build'], { cwd: projectRoot, stdio: 'inherit', shell: true });
  if (run.status !== 0) throw new Error('npm run win32:build 失败');
}

console.log(`启动原生外壳：ROM=${romPath}，测量 ${seconds} 秒`);
const app = startApp({ romPath, diagnostics: true });
let report;
try {
  await sleep(4000);
  // 窗口要在前台、没有被遮挡，画面才会被合成器按每帧刷新；这里只把窗口提到前台，不做别的操作
  drive(app.pid, ['wait:100']);
  await sleep(seconds * 1000);
  drive(app.pid, ['close']);
  const code = await Promise.race([app.exited, sleep(EXIT_LIMIT_MS).then(() => 'timeout')]);
  if (code !== 0) throw new Error(`关闭窗口后进程没有正常退出：${code}`);
  report = readDiagnostics(app);
} finally {
  app.stop();
}
try {
  if (!report) throw new Error('没有读到诊断文件');
  const { published, consumed, dropped, latency } = report;
  const dropRatio = published === 0 ? 1 : dropped / published;
  const renderRatio = published === 0 ? 0 : consumed / published;
  console.log(`核心送来 ${published} 帧，画出 ${consumed} 帧，被新帧顶掉 ${dropped} 帧（${(dropRatio * 100).toFixed(2)}%），运行 ${report.seconds.toFixed(1)} 秒`);
  console.log(`核心发布到画出的延迟：平均 ${ms(latency.averageMs)}，p95 ${ms(latency.p95Ms)}，最大 ${ms(latency.maxMs)}（${latency.count} 帧）`);
  const checks = [
    ['画出帧数不低于送来帧数的 98%', renderRatio >= RENDER_RATIO_LIMIT],
    ['丢帧不超过 1%', dropRatio <= DROP_RATIO_LIMIT],
    [`p95 延迟不超过两帧（${ms(LATENCY_LIMIT_MS)}）`, latency.p95Ms <= LATENCY_LIMIT_MS]
  ];
  for (const [name, ok] of checks) console.log(`${ok ? '通过' : '失败'}：${name}`);
  process.exitCode = checks.every(([, ok]) => ok) ? 0 : 1;
} finally {
  removeUserData(app);
}
