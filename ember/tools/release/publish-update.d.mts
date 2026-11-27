/*
 * ------------------------------------------------------------------------
 * 名称：publish-update.d.mts
 * 说明：publish-update.mjs 的类型声明，供测试引用。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-21
 * 备注：无。
 * ------------------------------------------------------------------------
 */

import type { KeyObject } from 'node:crypto';

export declare const LATEST_FILE: string;
export declare function buildLatest(privateKey: KeyObject, version: string, content: Buffer): string;
export declare function planUploads(version: string): Array<{ file: string; key: string }>;
export declare function publicKeyMatches(privateKey: KeyObject, headerText: string): boolean;
