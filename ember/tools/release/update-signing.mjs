/*
 * ------------------------------------------------------------------------
 * 名称：update-signing.mjs
 * 说明：升级清单的签名：私钥的位置与读取、公钥写成 C++ 头文件的内容、清单的生成与签名。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-21
 * 备注：私钥默认在与 famiclone 并列的文档仓库里（docs/01-文档/密钥/ember-update-signing-key.pem，PKCS#8 PEM，RSA 3072 位），环境变量 EMBER_UPDATE_KEY 可指定其他文件；不放进 Ember 仓库、不写进脚本，丢了就没法再发布能被已装程序接受的升级；
 *       被签名的文字与 win32/app/src/shared/update/update-manifest.cpp 的 updateSignedMessage 完全一致（ember-update、版本号、文件名、大小、SHA-256 五行，换行分隔，结尾没有换行）；签名是 RSA PKCS#1 v1.5 加 SHA-256。
 * ------------------------------------------------------------------------
 */

import { createHash, createPrivateKey, createPublicKey, createSign, generateKeyPairSync } from 'node:crypto';
import { existsSync, mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..', '..');
/** 签名私钥文件：默认在文档仓库的密钥目录（与 famiclone 并列）。 */
export const PRIVATE_KEY_FILE = process.env.EMBER_UPDATE_KEY ?? resolve(ROOT, '..', '..', 'docs', '01-文档', '密钥', 'ember-update-signing-key.pem');
/** 写出公钥的 C++ 头文件。 */
export const PUBLIC_KEY_HEADER = join(ROOT, 'win32', 'app', 'src', 'shared', 'update', 'update-public-key.hpp');
/** 密钥的位数。 */
export const KEY_BITS = 3072;

/** 被签名的文字。 */
export function signedMessage({ version, file, size, sha256 }) {
  return ['ember-update', version, file, String(size), sha256].join('\n');
}

/** 文件的 SHA-256（小写十六进制）与大小。 */
export function digestOf(content) {
  return { sha256: createHash('sha256').update(content).digest('hex'), size: content.length };
}

/** 用私钥给清单内容签名，返回完整的清单对象。 */
export function buildManifest(privateKey, { version, content }) {
  const file = `Ember-${version}.exe`;
  const { sha256, size } = digestOf(content);
  const signature = createSign('sha256').update(signedMessage({ version, file, size, sha256 })).sign(privateKey).toString('base64');
  return { version, file, size, sha256, signature };
}

/** 读取私钥；不存在时报错并说明怎么生成。 */
export function loadPrivateKey(file = PRIVATE_KEY_FILE) {
  if (!existsSync(file)) throw new Error(`找不到升级签名私钥：${file}（先运行 npm run update:keygen 生成，并自己备份好）`);
  return createPrivateKey(readFileSync(file, 'utf8'));
}

/** 公钥的模数与指数（大端字节）。 */
export function publicKeyParts(privateKey) {
  const jwk = createPublicKey(privateKey).export({ format: 'jwk' });
  return { modulus: Buffer.from(jwk.n, 'base64url'), exponent: Buffer.from(jwk.e, 'base64url') };
}

/** 一组字节写成 C++ 数组内容，每行 12 个。 */
function bytesText(bytes) {
  const rows = [];
  for (let index = 0; index < bytes.length; index += 12) {
    rows.push(`    ${[...bytes.subarray(index, index + 12)].map((byte) => `0x${byte.toString(16).padStart(2, '0')}`).join(', ')}`);
  }
  return rows.join(',\n');
}

/** 公钥头文件的内容。 */
export function formatPublicKeyHeader({ modulus, exponent }) {
  return `/*
 * ------------------------------------------------------------------------
 * 名称：update-public-key.hpp
 * 说明：升级清单的签名公钥（RSA ${modulus.length * 8} 位）：模数与指数，大端字节。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-21
 * 备注：由 tools/release/keygen.mjs 生成（npm run update:keygen），对应私钥在文档仓库的 01-文档/密钥/ember-update-signing-key.pem，不要手改；换公钥后已发布的程序不再接受用旧私钥签名的升级。
 * ------------------------------------------------------------------------
 */

#pragma once

namespace ember::win32::shared {

/** 签名公钥的模数（大端）。 */
inline constexpr unsigned char kUpdatePublicModulus[] = {
${bytesText(modulus)}};

/** 签名公钥的指数（大端）。 */
inline constexpr unsigned char kUpdatePublicExponent[] = {
${bytesText(exponent)}};

}  // namespace ember::win32::shared
`;
}

/**
 * 生成签名密钥：私钥写到文档仓库的密钥目录，公钥写成头文件。私钥已存在时不覆盖，报错。
 * @returns 私钥与头文件的路径。
 */
export function generateSigningKey() {
  if (existsSync(PRIVATE_KEY_FILE)) throw new Error(`升级签名私钥已经存在：${PRIVATE_KEY_FILE}，不覆盖（覆盖后已发布的程序都不再接受升级）；要换密钥请先手动移走它`);
  const { privateKey } = generateKeyPairSync('rsa', { modulusLength: KEY_BITS, publicExponent: 0x10001 });
  mkdirSync(dirname(PRIVATE_KEY_FILE), { recursive: true });
  writeFileSync(PRIVATE_KEY_FILE, privateKey.export({ type: 'pkcs8', format: 'pem' }), { mode: 0o600 });
  writeFileSync(PUBLIC_KEY_HEADER, formatPublicKeyHeader(publicKeyParts(privateKey)));
  return { privateKey: PRIVATE_KEY_FILE, header: PUBLIC_KEY_HEADER };
}
