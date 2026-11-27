/*
 * ------------------------------------------------------------------------
 * 名称：third-party.d.mts
 * 说明：third-party.mjs 的类型声明，供测试引用。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-21
 * 备注：条目字段与各项目的 third-party.json 一致。
 * ------------------------------------------------------------------------
 */

/** 清单条目。 */
export interface ThirdPartyEntry {
  name: string;
  version: string;
  authors: string[];
  license: string;
  homepage: string;
  usage: string;
  bundled: boolean;
  licenseText: string;
}

export declare const DECLARATION_FILE: string;
export declare const NOTICES_FILE: string;
export declare const NOTICES_DIRECTORY: string;
export declare const NOTICES_SOURCE: string;
export declare function readDeclarations(directory: string): ThirdPartyEntry[];
export declare function collectNotices(): ThirdPartyEntry[];
export declare function formatNotices(entries: readonly ThirdPartyEntry[]): string;
export declare function formatNoticeSource(entries: readonly ThirdPartyEntry[]): string;
export declare function writeThirdPartyFiles(): { source: string; notices: string; count: number };
