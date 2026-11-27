/*
 * ------------------------------------------------------------------------
 * 名称：import-fcdev-docs.d.mts
 * 说明：fcdev 手册导入脚本的类型声明，供测试引用。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-18
 * ------------------------------------------------------------------------
 */

/** fcdev 扩展的使用文档主题登记项（与 vscode/docs/topics.json 一致）。 */
export interface FcdevDocTopic {
  readonly topic: string;
  readonly file: string;
  readonly root?: 'extension';
  readonly title: string;
  readonly summary: string;
}

/** 站点侧边栏的一个分组。 */
export interface SidebarGroup {
  readonly text: string;
  readonly items: { readonly text: string; readonly link: string }[];
}

/** fcdev 扩展的使用文档主题登记表。 */
export const DOC_TOPICS: readonly FcdevDocTopic[];

/** 仓库根目录。 */
export const REPO_ROOT: string;
/** fcdev 扩展目录。 */
export const EXTENSION_ROOT: string;
/** 导入的目标目录（ember/help/fcdev）。 */
export const HELP_FCDEV_DIRECTORY: string;

/** 主题所属的分组名。 */
export function groupTitleOf(topic: { readonly file: string }): string;
/** 把主题按分组归类，分组顺序固定，空分组不返回。 */
export function groupTopics<T extends { readonly file: string }>(topics: readonly T[]): { title: string; topics: T[] }[];
/** 站点侧边栏：概述加各分组。 */
export function sidebarGroups(topics?: readonly FcdevDocTopic[]): SidebarGroup[];
/** 主题的源文件绝对路径。 */
export function topicSourceFile(topic: { readonly file: string; readonly root?: 'extension' }, extensionRoot?: string): string;
/** 把一份文档转成帮助站点的页面。 */
export function convertDocument(input: { text: string; sourceFile: string; relativeFile: string; pageByFile: ReadonlyMap<string, string> }): string;
/** 概述页。 */
export function renderOverview(topics: readonly FcdevDocTopic[]): string;
/** 导入全部文档到目标目录，返回页数与实际写入的页数。 */
export function importFcdevDocs(options?: { topics?: readonly FcdevDocTopic[]; extensionRoot?: string; outputDirectory?: string }): { pages: number; written: number };
