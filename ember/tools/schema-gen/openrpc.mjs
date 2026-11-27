/*
 * ------------------------------------------------------------------------
 * 名称：openrpc.mjs
 * 说明：由各模块的 Schema 生成 OpenRPC 1.3 接口描述文档：全部请求（含只给调试服务的）作为方法，参数按名称传递，事件放在扩展字段 x-events，数据类型放在 components.schemas。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * 备注：文档写到 FC Dev 扩展的 vscode/debug/openrpc.json（给协议参考与客户端生成工具用）与核心的 C++ 常量（调试服务的 rpc.discover 返回它）；
 *       主进程自己处理的请求（mainRequests）不在调试服务里，不收录；协议与报文约定见 docs/04-调试服务.md。
 * ------------------------------------------------------------------------
 */

/** 协议版本。 */
export const PROTOCOL_VERSION = 1;

/** 应用错误的 JSON-RPC 错误码：具体错误码放在 error.data.code。 */
const APPLICATION_ERROR = -32000;

/** 类型引用转成 components.schemas 里的路径。 */
function refPath(ref, currentModule) {
  const match = /^(?:([a-z][a-z0-9-]*))?#\/types\/([A-Z][A-Za-z0-9]*)$/.exec(ref);
  return `#/components/schemas/${match[1] ?? currentModule}.${match[2]}`;
}

/** Schema 节点转成 JSON Schema。 */
function toJsonSchema(node, currentModule) {
  if (node.$ref !== undefined) {
    const schema = { $ref: refPath(node.$ref, currentModule) };
    if (node.description !== undefined) schema.description = node.description;
    return schema;
  }
  const schema = {};
  for (const key of ['type', 'description', 'enum', 'minimum', 'maximum', 'minLength', 'maxLength', 'minItems', 'maxItems']) {
    if (node[key] !== undefined) schema[key] = node[key];
  }
  if (node.items !== undefined) schema.items = toJsonSchema(node.items, currentModule);
  if (node.type === 'object') {
    schema.properties = Object.fromEntries(Object.entries(node.properties ?? {}).map(([name, child]) => [name, toJsonSchema(child, currentModule)]));
    schema.required = node.required ?? [];
    schema.additionalProperties = false;
  }
  return schema;
}

/** 顶层的 data 属性没有类型时是二进制（协议约定，见 docs/04-调试服务.md 第 2.3 节），加 x-binary 标记。 */
function markBinaryData(schema) {
  const data = schema.properties?.data;
  if (data !== undefined && data.type === undefined && data.$ref === undefined) data['x-binary'] = true;
  return schema;
}

/** 请求的参数对象转成 OpenRPC 的按名称参数。 */
function toParams(node, currentModule) {
  const required = new Set(node.required ?? []);
  return Object.entries(node.properties ?? {}).map(([name, child]) => {
    const schema = toJsonSchema(child, currentModule);
    const param = { name, required: required.has(name), schema };
    if (child.description !== undefined) param.description = child.description;
    return param;
  });
}

/**
 * 生成 OpenRPC 文档。
 * @param {Array<{name: string, schema: object}>} modules 各模块的 Schema。
 * @returns {object} 文档对象。
 */
export function generateOpenRpc(modules) {
  const methods = [];
  const events = [];
  const schemas = {};
  for (const { name, schema } of modules) {
    for (const [typeName, node] of Object.entries(schema.types ?? {})) schemas[`${name}.${typeName}`] = toJsonSchema(node, name);
    for (const [operation, request] of Object.entries(schema.requests ?? {})) {
      const method = {
        name: `${name}.${operation}`,
        summary: request.description.split(/[。；]/)[0],
        description: request.description,
        tags: [{ name }],
        paramStructure: 'by-name',
        params: toParams(request.params, name),
        result: { name: 'result', schema: markBinaryData(toJsonSchema(request.result, name)) },
        errors: (request.errors ?? []).map((code) => ({ code: APPLICATION_ERROR, message: code, data: { code } }))
      };
      if (request.debugOnly === true) method['x-debugOnly'] = true;
      if (request.mainOnly === true) method['x-mainOnly'] = true;
      methods.push(method);
    }
    for (const [event, definition] of Object.entries(schema.events ?? {})) {
      events.push({ name: `${name}.${event}`, description: definition.description ?? '', tags: [{ name }], data: markBinaryData(toJsonSchema(definition.data, name)) });
    }
  }
  methods.push({
    name: 'rpc.discover',
    summary: '返回本文档',
    description: '返回描述全部请求与事件的 OpenRPC 文档，工具可据此生成客户端与界面。',
    tags: [{ name: 'rpc' }],
    paramStructure: 'by-name',
    params: [],
    result: { name: 'document', schema: { type: 'object' } }
  });
  return {
    openrpc: '1.3.2',
    info: {
      title: 'Ember 调试服务',
      version: String(PROTOCOL_VERSION),
      description: '经 WebSocket 传输的 JSON-RPC 2.0 接口；连接、握手、令牌、二进制帧与事件订阅的约定见 docs/04-调试服务.md。'
    },
    servers: [{ name: '本机调试服务', url: 'ws://127.0.0.1:{port}/', variables: { port: { default: '5688', description: '首选项里设置的端口，或启动参数 --debug-port' } } }],
    methods,
    components: { schemas },
    'x-events': events
  };
}

/** UTF-8 字节数。 */
function byteLength(text) {
  return Buffer.byteLength(text, 'utf8');
}

/** 把文本按字符切成每段不超过 maxBytes 字节的若干段（单个字符串字面量有长度上限）。 */
export function splitByBytes(text, maxBytes) {
  const chunks = [];
  let current = '';
  for (const char of text) {
    if (byteLength(current) + byteLength(char) > maxBytes) {
      chunks.push(current);
      current = '';
    }
    current += char;
  }
  if (current !== '') chunks.push(current);
  return chunks;
}

/** 生成 C++ 头文件与源文件：openRpcDocument() 返回文档的 JSON 文本。 */
export function generateOpenRpcCpp(document, header) {
  const text = JSON.stringify(document);
  const chunks = splitByBytes(text, 6000);
  const literals = chunks.map((chunk, index) => {
    // 原始字符串的定界符里不能出现在内容里的写法。
    if (chunk.includes(')OPENRPC"')) throw new Error(`OpenRPC 文档第 ${index + 1} 段含有原始字符串的结束标记`);
    return `    R"OPENRPC(${chunk})OPENRPC"`;
  });
  return {
    header: `${header}\n#pragma once\n\n#include <string>\n\nnamespace ember::contracts::debug {\n\n/** 调试服务的接口描述文档（OpenRPC，JSON 文本），由全部模块的 Schema 生成。 */\nstd::string openRpcDocument();\n\n}  // namespace ember::contracts::debug\n`,
    source: `${header}#include "contracts/debug/generated/openrpc.hpp"\n\nnamespace ember::contracts::debug {\n\nstd::string openRpcDocument() {\n  // 单个字符串字面量有长度上限，所以分段书写再拼接。\n  static const char* const CHUNKS[] = {\n${literals.join(',\n')}};\n  std::string document;\n  for (const char* chunk : CHUNKS) document += chunk;\n  return document;\n}\n\n}  // namespace ember::contracts::debug\n`
  };
}
