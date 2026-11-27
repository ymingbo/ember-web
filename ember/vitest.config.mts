/*
 * ------------------------------------------------------------------------
 * 名称：vitest.config.mts
 * 说明：工具脚本的测试配置：测试放在 test/，在 Node 环境运行。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-20
 * 备注：C++ 部分的测试由 npm run core:test 与 npm run win32:test 运行。
 * ------------------------------------------------------------------------
 */

import { defineConfig } from 'vitest/config';

export default defineConfig({
  test: {
    environment: 'node',
    globals: true,
    include: ['test/**/*.test.ts']
  }
});
