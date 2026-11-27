/*
 * ------------------------------------------------------------------------
 * 名称：pack-resources.d.mts
 * 说明：pack-resources.mjs 的类型声明，供测试引用。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-21
 * 备注：无。
 * ------------------------------------------------------------------------
 */

export declare const RESOURCE_ARCHIVE_FILE: string;

/** 打包资源的目录；都不给时取构建目录。 */
export interface PackResourcesOptions {
  outDirectory?: string;
  pluginsDirectory?: string;
  helpDirectory?: string;
  noticesFile?: string;
}

export declare function packResources(options?: PackResourcesOptions): { file: string; entries: number; skipped: string[] };
