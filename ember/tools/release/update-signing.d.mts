/*
 * ------------------------------------------------------------------------
 * 名称：update-signing.d.mts
 * 说明：update-signing.mjs 的类型声明，供测试与其他脚本引用。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-21
 * 备注：无。
 * ------------------------------------------------------------------------
 */

import type { KeyObject } from 'node:crypto';

export declare const PRIVATE_KEY_FILE: string;
export declare const PUBLIC_KEY_HEADER: string;
export declare const KEY_BITS: number;

/** 清单里被签名的各项。 */
export interface SignedFields {
  version: string;
  file: string;
  size: number;
  sha256: string;
}

/** 升级清单。 */
export interface UpdateManifestFile extends SignedFields {
  signature: string;
}

export declare function signedMessage(fields: SignedFields): string;
export declare function digestOf(content: Buffer): { sha256: string; size: number };
export declare function buildManifest(privateKey: KeyObject, input: { version: string; content: Buffer }): UpdateManifestFile;
export declare function loadPrivateKey(file?: string): KeyObject;
export declare function publicKeyParts(privateKey: KeyObject): { modulus: Buffer; exponent: Buffer };
export declare function formatPublicKeyHeader(parts: { modulus: Buffer; exponent: Buffer }): string;
export declare function generateSigningKey(): { privateKey: string; header: string };
