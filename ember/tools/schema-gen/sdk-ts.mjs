/*
 * ------------------------------------------------------------------------
 * 名称：sdk-ts.mjs
 * 说明：由 OpenRPC 文档生成调试客户端的 TypeScript 类型文件：数据类型、每个请求的参数与结果、请求表（RequestMap）、事件表（EventMap）与按模块分组的接口（EmberApi）。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * 备注：只依赖 OpenRPC 文档，不读各模块的 Schema；无类型的顶层 data 属性（x-binary）生成 Uint8Array。
 * ------------------------------------------------------------------------
 */

const HEADER = '// 由 ember 的 tools/schema-gen 根据 schema/*.schema.json 生成，不要手改。\n/* eslint-disable */\n\n';

/** kebab-case 转 PascalCase。 */
function pascal(name) {
  return name
    .split(/[-_]/)
    .map((part) => part.charAt(0).toUpperCase() + part.slice(1))
    .join('');
}

/** kebab-case 转 camelCase。 */
function camel(name) {
  const text = pascal(name);
  return text.charAt(0).toLowerCase() + text.slice(1);
}

/** 文档注释的内容：去掉会提前结束注释的写法。 */
function commentText(text) {
  return text.replaceAll('*/', '*\\/');
}

/** components.schemas 里的名字（模块.类型）转成 TypeScript 类型名；类型名已带模块名前缀时不重复。 */
function typeName(key) {
  const [module, name] = key.split('.');
  const prefix = pascal(module);
  return name.startsWith(prefix) ? name : `${prefix}${name}`;
}

/** 引用路径转类型名。 */
function refName(ref) {
  return typeName(ref.slice('#/components/schemas/'.length));
}

/** 带文档注释的一行或多行。 */
function withComment(description, indent, line) {
  return description === undefined ? line : `${indent}/** ${commentText(description)} */\n${line}`;
}

/** JSON Schema 节点转 TypeScript 类型表达式；对象按缩进展开。 */
function schemaToTs(schema, indent) {
  if (schema.$ref !== undefined) return refName(schema.$ref);
  if (schema['x-binary'] === true) return 'Uint8Array';
  if (schema.type === undefined) return 'unknown';
  const types = Array.isArray(schema.type) ? schema.type : [schema.type];
  const nullable = types.includes('null');
  const type = types.find((item) => item !== 'null');
  let base;
  if (type === 'array') {
    const item = schemaToTs(schema.items, indent);
    base = /[|&]/.test(item) ? `Array<${item}>` : `${item}[]`;
  } else if (type === 'string') {
    base = schema.enum === undefined ? 'string' : schema.enum.map((value) => JSON.stringify(value)).join(' | ');
  } else if (type === 'integer' || type === 'number') {
    base = 'number';
  } else if (type === 'boolean') {
    base = 'boolean';
  } else if (type === 'object') {
    base = schema.properties === undefined ? 'Record<string, unknown>' : objectToTs(schema.properties, new Set(schema.required ?? []), indent);
  } else {
    throw new Error(`不支持的类型：${JSON.stringify(schema.type)}`);
  }
  return nullable ? `${base} | null` : base;
}

/** 对象的属性表转成 TypeScript 对象类型。 */
function objectToTs(properties, required, indent) {
  const entries = Object.entries(properties);
  if (entries.length === 0) return 'Record<string, never>';
  const inner = `${indent}  `;
  const lines = entries.map(([name, child]) => withComment(child.description, inner, `${inner}${name}${required.has(name) ? '' : '?'}: ${schemaToTs(child, inner)};`));
  return `{\n${lines.join('\n')}\n${indent}}`;
}

/** 一个命名的对象类型声明。 */
function declareObject(name, description, schema) {
  const body = schemaToTs(schema, '');
  const declaration = body.startsWith('{') ? `export interface ${name} ${body}` : `export type ${name} = ${body};`;
  return withComment(description, '', declaration);
}

/** 请求的参数列表转成对象 Schema。 */
function paramsSchema(params) {
  return {
    type: 'object',
    properties: Object.fromEntries(params.map((param) => [param.name, { ...param.schema, description: param.description ?? param.schema.description }])),
    required: params.filter((param) => param.required).map((param) => param.name)
  };
}

/**
 * 生成 TypeScript 类型文件的内容。
 * @param {object} document OpenRPC 文档（generateOpenRpc 的结果）。
 * @returns {string} 文件内容。
 */
export function generateSdkTypes(document) {
  const names = new Map();
  const declare = (name, source) => {
    if (names.has(name)) throw new Error(`类型名重复：${name}（${names.get(name)} 与 ${source}）`);
    names.set(name, source);
    return name;
  };
  const blocks = [];

  for (const [key, schema] of Object.entries(document.components.schemas)) {
    blocks.push(declareObject(declare(typeName(key), key), schema.description, schema));
  }

  const requests = [];
  const api = new Map();
  for (const method of document.methods) {
    const [module, operation] = method.name.split('.');
    const base = `${pascal(module)}${pascal(operation)}`;
    const hasParams = method.params.length > 0;
    const paramsType = declare(`${base}Params`, method.name);
    const resultType = declare(`${base}Result`, method.name);
    blocks.push(declareObject(paramsType, `${method.name} 的参数。`, paramsSchema(method.params)));
    const resultSchema = method.result.schema;
    blocks.push(declareObject(resultType, `${method.name} 的结果。`, resultSchema));
    requests.push(`  ${JSON.stringify(method.name)}: { params: ${paramsType}; result: ${resultType} };`);
    const requiredParams = method.params.some((param) => param.required);
    const parameter = hasParams ? `params${requiredParams ? '' : '?'}: ${paramsType}` : '';
    const line = withComment(method.description, '    ', `    ${operation}(${parameter}): Promise<${resultType}>;`);
    if (!api.has(module)) api.set(module, []);
    api.get(module).push(line);
  }

  const events = [];
  for (const event of document['x-events']) {
    const [module, operation] = event.name.split('.');
    const dataType = declare(`${pascal(module)}${pascal(operation)}Data`, event.name);
    blocks.push(declareObject(dataType, `${event.name} 事件的数据。`, event.data));
    events.push(withComment(event.description === '' ? undefined : event.description, '  ', `  ${JSON.stringify(event.name)}: ${dataType};`));
  }

  const apiLines = [...api.entries()].map(([module, lines]) => `  readonly ${camel(module)}: {\n${lines.join('\n')}\n  };`);
  return [
    HEADER,
    `/** 调试协议的版本，握手时校验。 */\nexport const PROTOCOL_VERSION = ${Number(document.info.version)};\n\n`,
    `${blocks.join('\n\n')}\n\n`,
    `/** 全部请求：方法名对应参数与结果。 */\nexport interface RequestMap {\n${requests.join('\n')}\n}\n\n`,
    `/** 全部事件：事件名对应数据；订阅后以通知的形式送达。 */\nexport interface EventMap {\n${events.join('\n')}\n}\n\n`,
    `/** 按模块分组的请求接口，每个方法对应同名的请求。 */\nexport interface EmberApi {\n${apiLines.join('\n')}\n}\n`
  ].join('');
}
