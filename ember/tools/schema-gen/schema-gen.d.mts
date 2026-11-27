/*
 * ------------------------------------------------------------------------
 * 名称：schema-gen.d.mts
 * 说明：Schema 代码生成脚本的类型声明，供测试与其他脚本引用。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-13
 * ------------------------------------------------------------------------
 */

/** 一个模块的 Schema 及其来源文件。 */
export interface LoadedSchema {
  readonly name: string;
  readonly file: string;
  readonly schema: Record<string, unknown>;
  readonly refs: readonly { readonly module: string; readonly name: string }[];
}

/** 读取并检查全部模块的 Schema。 */
export function loadSchemas(root: string): LoadedSchema[];

/** 生成全部代码；给出 sdkDirectory 且该目录存在时，另把接口文档与类型写到它的 generated。 */
export function generate(root: string, options?: { sdkDirectory?: string }): { written: string[]; removed: string[]; modules: string[] };
