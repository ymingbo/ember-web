/*
 * ------------------------------------------------------------------------
 * 名称：check-modules.d.mts
 * 说明：check-modules.mjs 的类型声明，供测试引用。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-13
 * ------------------------------------------------------------------------
 */

/** 检查结果：错误已去重，edges 是模块之间的依赖。 */
export interface ProjectCheck {
  errors: string[];
  edges: Array<{ from: string; to: string }>;
  schemaCount: number;
}

/** 检查一个项目目录的分层、模块依赖与模块登记。 */
export function checkProject(root: string): ProjectCheck;
