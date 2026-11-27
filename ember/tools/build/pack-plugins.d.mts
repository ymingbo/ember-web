/*
 * ------------------------------------------------------------------------
 * 名称：pack-plugins.d.mts
 * 说明：插件打包脚本的类型声明，供测试与其他脚本引用。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * ------------------------------------------------------------------------
 */

/** 打出的插件包。 */
export interface PackedPlugin {
  readonly id: string;
  readonly file: string;
}

/** 生成 zip：每个文件一个条目，可以带一段末尾注释。 */
export function createZip(entries: ReadonlyArray<{ name: string; data: Buffer }>, comment?: Buffer): Buffer;

/** 打包一个插件目录到输出目录。 */
export function packPlugin(sourceDir: string, outDir: string): PackedPlugin;

/** 打包 plugins/ 下的全部插件，默认输出到 build/ember/plugins/。 */
export function packPlugins(root: string, outDir?: string): PackedPlugin[];
