/*
 * ------------------------------------------------------------------------
 * 名称：keygen.mjs
 * 说明：生成升级清单的签名密钥（npm run update:keygen）：私钥写到文档仓库的 01-文档/密钥/ember-update-signing-key.pem，公钥写成 win32/app/src/shared/update/update-public-key.hpp（提交进仓库，编译进程序）。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-21
 * 备注：私钥已经存在时不覆盖；私钥丢了就没法再发布已装程序能接受的升级；参数 --export-public 只用现有私钥重新写出公钥头文件。
 * ------------------------------------------------------------------------
 */

import { writeFileSync } from 'node:fs';
import { PRIVATE_KEY_FILE, PUBLIC_KEY_HEADER, formatPublicKeyHeader, generateSigningKey, loadPrivateKey, publicKeyParts } from './update-signing.mjs';

try {
  if (process.argv.includes('--export-public')) {
    writeFileSync(PUBLIC_KEY_HEADER, formatPublicKeyHeader(publicKeyParts(loadPrivateKey())));
    console.log(`已用现有私钥重新写出公钥：${PUBLIC_KEY_HEADER}`);
  } else {
    const { privateKey, header } = generateSigningKey();
    console.log(`升级签名私钥：${privateKey}`);
    console.log(`公钥头文件：${header}（要提交）`);
    console.log(`注意：私钥只此一份，丢了就没法再发布已装程序能接受的升级，记得把文档仓库里的这个文件提交备份。当前位置：${PRIVATE_KEY_FILE}`);
  }
} catch (error) {
  console.error(error instanceof Error ? error.message : String(error));
  process.exit(1);
}
