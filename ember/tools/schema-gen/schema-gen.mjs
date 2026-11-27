/*
 * ------------------------------------------------------------------------
 * 名称：schema-gen.mjs
 * 说明：Schema 代码生成：读取 schema/<模块>.schema.json，生成 C++ 的数据结构、请求名常量、JSON 转换与结构校验，
 *       以及原生外壳的请求目录、调试服务的接口描述。
 *       外壳自己处理的请求（mainRequests）不生成 C++ 数据结构，只进入原生外壳的请求目录；
 *       请求可标记 mainOnly（只能由外壳发起）或 debugOnly（只经调试服务开放），两者都不属于界面可发起的请求。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-13
 * 备注：只支持 JSON Schema 的子集（见 docs/06-构建与模块登记.md 第 2 节）；本仓库的生成目录不提交，内容没变的文件不改写；
 *       已删除的模块，其生成目录一并删除；VS Code 扩展（vscode/）里调试客户端用的接口文档与类型一并写入（接口文档 vscode/debug/openrpc.json，类型 vscode/src/contracts/debug-client/generated/api.ts）。可作为模块被测试引用（generate），也可直接运行。
 * ------------------------------------------------------------------------
 */

import { existsSync, mkdirSync, readdirSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { generateOpenRpc, generateOpenRpcCpp } from './openrpc.mjs';
import { generateSdkTypes } from './sdk-ts.mjs';

const MODULE_NAME_PATTERN = /^[a-z][a-z0-9]*(-[a-z0-9]+)*$/;
/** 各模块 Schema 文件名的后缀，前面是模块名。 */
const SCHEMA_FILE_SUFFIX = '.schema.json';
const MEMBER_NAME_PATTERN = /^[a-z][A-Za-z0-9]*$/;
const TYPE_NAME_PATTERN = /^[A-Z][A-Za-z0-9]*$/;
const NODE_KEYWORDS = new Set(['type', 'description', 'properties', 'required', 'items', 'minItems', 'maxItems', 'minLength', 'maxLength', 'enum', 'minimum', 'maximum', '$ref']);
const PRIMITIVES = new Set(['string', 'integer', 'number', 'boolean']);
/** 属性名会成为 C++ 结构体的成员名，不能是 C++ 关键字。 */
const CPP_KEYWORDS = new Set(
  'alignas alignof and asm auto bitand bitor bool break case catch char char8_t char16_t char32_t class compl concept const consteval constexpr constinit const_cast continue co_await co_return co_yield decltype default delete do double dynamic_cast else enum explicit export extern false float for friend goto if inline int long mutable namespace new noexcept not nullptr operator or private protected public register reinterpret_cast requires return short signed sizeof static static_assert static_cast struct switch template this thread_local throw true try typedef typeid typename union unsigned using virtual void volatile wchar_t while xor'.split(' ')
);
const CPP_HEADER = (source) => `// 由 tools/schema-gen/schema-gen.mjs 根据 ${source} 生成，不要手改。\n\n`;

/** kebab-case 转 PascalCase。 */
function pascal(name) {
  return name
    .split(/[-_]/)
    .map((part) => part.charAt(0).toUpperCase() + part.slice(1))
    .join('');
}

/** kebab-case 或 camelCase 转 snake_case。 */
function snake(name) {
  return name
    .replace(/-/g, '_')
    .replace(/([a-z0-9])([A-Z])/g, '$1_$2')
    .toLowerCase();
}

/** 抛出带位置的格式错误。 */
function fail(file, path, message) {
  throw new Error(`${file}：${path} ${message}`);
}

/** 解析 $ref：返回引用的模块与类型名。 */
function parseRef(ref, currentModule, file, path) {
  const match = /^(?:([a-z][a-z0-9-]*))?#\/types\/([A-Z][A-Za-z0-9]*)$/.exec(ref);
  if (!match) fail(file, path, `的 $ref 写法不对：${ref}，应为 #/types/名称 或 模块#/types/名称`);
  return { module: match[1] ?? currentModule, name: match[2] };
}

/** 节点的类型与是否可空；没有 type 也没有 $ref 的节点是任意 JSON。 */
function typeOf(node) {
  if (node.type === undefined && node.$ref === undefined) return { type: 'any', nullable: false };
  if (Array.isArray(node.type)) {
    const rest = node.type.filter((type) => type !== 'null');
    return { type: rest[0], nullable: node.type.includes('null') };
  }
  return { type: node.type, nullable: false };
}

/** 检查一个取值节点（属性、数组项）。 */
function checkValueNode(node, file, path, currentModule, refs) {
  if (node === null || typeof node !== 'object' || Array.isArray(node)) fail(file, path, '必须是对象');
  for (const key of Object.keys(node)) if (!NODE_KEYWORDS.has(key)) fail(file, path, `不支持的关键字：${key}`);
  if (node.$ref !== undefined) {
    refs.push(parseRef(node.$ref, currentModule, file, path));
    return;
  }
  if (node.type === undefined) {
    for (const key of Object.keys(node)) if (key !== 'description') fail(file, path, `是任意 JSON，只能写 description，不能写 ${key}`);
    return;
  }
  const { type } = typeOf(node);
  if (Array.isArray(node.type) && (node.type.length !== 2 || !node.type.includes('null'))) fail(file, path, '可空类型只能写成 ["类型", "null"]');
  if (type === 'object') fail(file, path, '属性不能直接写对象，请在 types 中定义后用 $ref 引用');
  if (type === 'array') {
    if (node.items === undefined) fail(file, path, '数组必须有 items');
    checkValueNode(node.items, file, `${path}.items`, currentModule, refs);
    return;
  }
  if (!PRIMITIVES.has(type)) fail(file, path, `不支持的类型：${String(node.type)}`);
  if (node.enum !== undefined && (type !== 'string' || !Array.isArray(node.enum) || node.enum.some((item) => typeof item !== 'string'))) {
    fail(file, path, '只支持字符串枚举');
  }
}

/** 检查一个对象节点（类型定义、请求参数与结果、事件数据）。 */
function checkObjectNode(node, file, path, currentModule, refs) {
  if (node === null || typeof node !== 'object' || node.type !== 'object') fail(file, path, '必须是 type 为 object 的对象');
  for (const key of Object.keys(node)) if (!['type', 'description', 'properties', 'required'].includes(key)) fail(file, path, `不支持的关键字：${key}`);
  const properties = node.properties ?? {};
  const required = node.required ?? [];
  for (const name of required) if (!(name in properties)) fail(file, path, `的 required 中有不存在的属性：${name}`);
  for (const [name, child] of Object.entries(properties)) {
    if (!MEMBER_NAME_PATTERN.test(name)) fail(file, `${path}.${name}`, '属性名必须是 camelCase');
    if (CPP_KEYWORDS.has(name)) fail(file, `${path}.${name}`, '属性名不能是 C++ 关键字');
    checkValueNode(child, file, `${path}.${name}`, currentModule, refs);
  }
}

/**
 * 读取并检查全部模块的 Schema。
 * @param {string} root 项目根目录。
 * @returns {Array<object>} 各模块的 Schema（按模块名排序）。
 */
export function loadSchemas(root) {
  const schemaDir = join(root, 'schema');
  if (!existsSync(schemaDir)) return [];
  const modules = [];
  for (const fileName of readdirSync(schemaDir).toSorted()) {
    if (!fileName.endsWith(SCHEMA_FILE_SUFFIX)) throw new Error(`schema/${fileName} 不是 <模块>${SCHEMA_FILE_SUFFIX}，schema/ 下只放各模块的 Schema 文件`);
    const name = fileName.slice(0, -SCHEMA_FILE_SUFFIX.length);
    const file = join(schemaDir, fileName);
    if (!MODULE_NAME_PATTERN.test(name)) throw new Error(`模块名必须是 kebab-case：${name}`);
    const relative = `schema/${fileName}`;
    const schema = JSON.parse(readFileSync(file, 'utf8'));
    if (schema.module !== name) fail(relative, 'module', `必须与文件名一致：${name}`);
    for (const key of Object.keys(schema)) if (!['$comment', 'module', 'types', 'requests', 'mainRequests', 'events'].includes(key)) fail(relative, key, '是不支持的顶层字段');
    const refs = [];
    for (const [typeName, node] of Object.entries(schema.types ?? {})) {
      if (!TYPE_NAME_PATTERN.test(typeName)) fail(relative, `types.${typeName}`, '类型名必须是 PascalCase');
      checkObjectNode(node, relative, `types.${typeName}`, name, refs);
    }
    for (const [operation, request] of Object.entries(schema.requests ?? {})) {
      if (!MEMBER_NAME_PATTERN.test(operation)) fail(relative, `requests.${operation}`, '操作名必须是 camelCase');
      for (const key of Object.keys(request)) if (!['description', 'params', 'result', 'errors', 'mainOnly', 'debugOnly'].includes(key)) fail(relative, `requests.${operation}.${key}`, '是不支持的字段');
      if (request.mainOnly !== undefined && request.mainOnly !== true) fail(relative, `requests.${operation}.mainOnly`, '只能写 true');
      if (request.debugOnly !== undefined && request.debugOnly !== true) fail(relative, `requests.${operation}.debugOnly`, '只能写 true');
      if (request.mainOnly === true && request.debugOnly === true) fail(relative, `requests.${operation}`, '不能同时标记 mainOnly 与 debugOnly');
      checkObjectNode(request.params, relative, `requests.${operation}.params`, name, refs);
      checkObjectNode(request.result, relative, `requests.${operation}.result`, name, refs);
      for (const code of request.errors ?? []) if (!code.startsWith(`${name}.`)) fail(relative, `requests.${operation}.errors`, `的错误码必须以模块名开头：${code}`);
    }
    // 主进程处理的请求：界面经预加载脚本发给主进程，不进入核心；参数与结果的类型名与核心请求共用一套命名，所以操作名不能重复。
    for (const [operation, request] of Object.entries(schema.mainRequests ?? {})) {
      if (!MEMBER_NAME_PATTERN.test(operation)) fail(relative, `mainRequests.${operation}`, '操作名必须是 camelCase');
      if (operation in (schema.requests ?? {})) fail(relative, `mainRequests.${operation}`, '与 requests 中的操作重名');
      for (const key of Object.keys(request)) if (!['description', 'params', 'result', 'errors'].includes(key)) fail(relative, `mainRequests.${operation}.${key}`, '是不支持的字段');
      checkObjectNode(request.params, relative, `mainRequests.${operation}.params`, name, refs);
      checkObjectNode(request.result, relative, `mainRequests.${operation}.result`, name, refs);
      for (const code of request.errors ?? []) if (!code.startsWith(`${name}.`)) fail(relative, `mainRequests.${operation}.errors`, `的错误码必须以模块名开头：${code}`);
    }
    for (const [event, definition] of Object.entries(schema.events ?? {})) {
      if (!MEMBER_NAME_PATTERN.test(event)) fail(relative, `events.${event}`, '事件名必须是 camelCase');
      checkObjectNode(definition.data, relative, `events.${event}.data`, name, refs);
    }
    modules.push({ name, file: relative, schema, refs });
  }
  // 引用的模块与类型必须存在。
  const byName = new Map(modules.map((module) => [module.name, module]));
  for (const module of modules) {
    for (const ref of module.refs) {
      const target = byName.get(ref.module);
      if (target === undefined || !(ref.name in (target.schema.types ?? {}))) throw new Error(`${module.file}：引用的类型不存在：${ref.module}#/types/${ref.name}`);
    }
  }
  return modules;
}

// ---------------------------------------------------------------- C++

/** 原生外壳（win32/app）的请求目录：核心请求按“界面可发起、只能由外壳发起、只经调试服务开放”分类，再列出外壳自己处理的请求。 */
function generateAppRequestCatalog(modules) {
  const coreEntries = modules.flatMap((module) =>
    Object.entries(module.schema.requests ?? {}).map(([operation, request]) => {
      const kind = request.mainOnly === true ? 'MainOnly' : request.debugOnly === true ? 'DebugOnly' : 'Ui';
      return `    CatalogEntry{${cppString(`${module.name}.${operation}`)}, RequestKind::${kind}}`;
    })
  );
  const mainNames = modules.flatMap((module) => Object.keys(module.schema.mainRequests ?? {}).map((operation) => `    ${cppString(`${module.name}.${operation}`)}`));
  return [
    CPP_HEADER('schema/*.schema.json'),
    '#pragma once',
    '',
    '#include <string_view>',
    '',
    'namespace ember::win32::contracts {',
    '',
    '/** 核心请求的类别。 */',
    'enum class RequestKind {',
    '  /** 界面可以发起。 */',
    '  Ui,',
    '  /** 只能由外壳自己发起（如 cartridges.load）。 */',
    '  MainOnly,',
    '  /** 只经调试服务开放。 */',
    '  DebugOnly',
    '};',
    '',
    '/** 核心的一个请求。 */',
    'struct CatalogEntry {',
    '  std::string_view name;',
    '  RequestKind kind;',
    '};',
    '',
    '/** 核心的全部请求。 */',
    `inline constexpr CatalogEntry kCoreRequests[] = {\n${coreEntries.join(',\n')}};`,
    '',
    '/** 由外壳自己处理的请求。 */',
    `inline constexpr std::string_view kShellRequests[] = {\n${mainNames.join(',\n')}};`,
    '',
    '}  // namespace ember::win32::contracts',
    ''
  ].join('\n');
}

/** 取值节点的 C++ 类型（不含可选包装）。 */
function cppBaseType(node, currentModule, includes) {
  if (node.$ref !== undefined) {
    const ref = parseRef(node.$ref, currentModule, '', '');
    if (ref.module !== currentModule) {
      includes.add(ref.module);
      return `::ember::contracts::${snake(ref.module)}::${ref.name}`;
    }
    return ref.name;
  }
  const { type } = typeOf(node);
  if (type === 'any') return 'nlohmann::json';
  if (type === 'array') return `std::vector<${cppValueType(node.items, currentModule, includes)}>`;
  if (type === 'string') return 'std::string';
  if (type === 'integer') return 'std::int64_t';
  if (type === 'number') return 'double';
  return 'bool';
}

/** 取值节点的 C++ 类型（可空时为 std::optional）。 */
function cppValueType(node, currentModule, includes) {
  const base = cppBaseType(node, currentModule, includes);
  return typeOf(node).nullable ? `std::optional<${base}>` : base;
}

/** C++ 字符串字面量。 */
function cppString(value) {
  return `"${String(value).replace(/\\/g, '\\\\').replace(/"/g, '\\"')}"`;
}

/** 可选的长度、项数。 */
function optionalSize(value) {
  return value === undefined ? 'std::nullopt' : `std::optional<std::size_t>(${value})`;
}

/** 可选的整数范围。 */
function optionalInt(value) {
  return value === undefined ? 'std::nullopt' : `std::optional<std::int64_t>(${value})`;
}

/** 可选的数字范围。 */
function optionalDouble(value) {
  return value === undefined ? 'std::nullopt' : `std::optional<double>(${value})`;
}

/** 读取一个取值节点的 C++ 表达式。 */
function cppReadExpr(node, json, path, currentModule) {
  if (node.$ref !== undefined) {
    const ref = parseRef(node.$ref, currentModule, '', '');
    const qualifier = ref.module === currentModule ? '' : `::ember::contracts::${snake(ref.module)}::`;
    return `${qualifier}parse${ref.name}(${json}, ${path})`;
  }
  const { type } = typeOf(node);
  if (type === 'any') return `nlohmann::json(${json})`;
  if (type === 'string') {
    const allowed = node.enum ? `std::vector<std::string>{${node.enum.map(cppString).join(', ')}}` : 'std::vector<std::string>{}';
    return `json_fields::readString(${json}, ${path}, ${optionalSize(node.minLength)}, ${optionalSize(node.maxLength)}, ${allowed})`;
  }
  if (type === 'integer') return `json_fields::readInteger(${json}, ${path}, ${optionalInt(node.minimum)}, ${optionalInt(node.maximum)})`;
  if (type === 'number') return `json_fields::readNumber(${json}, ${path}, ${optionalDouble(node.minimum)}, ${optionalDouble(node.maximum)})`;
  if (type === 'boolean') return `json_fields::readBoolean(${json}, ${path})`;
  const itemType = cppValueType(node.items, currentModule, new Set());
  const item = cppReadNullable(node.items, 'item', 'itemPath', currentModule);
  return `json_fields::readArray<${itemType}>(${json}, ${path}, ${optionalSize(node.minItems)}, ${optionalSize(node.maxItems)}, [](const nlohmann::json& item, const std::string& itemPath) { return ${item}; })`;
}

/** 读取一个可能可空的取值节点。 */
function cppReadNullable(node, json, path, currentModule) {
  const expression = cppReadExpr(node, json, path, currentModule);
  if (!typeOf(node).nullable) return expression;
  const base = cppBaseType(node, currentModule, new Set());
  return `((${json}).is_null() ? std::optional<${base}>() : std::optional<${base}>(${expression}))`;
}

/** 对象节点的 C++ 结构体声明。 */
function cppStruct(name, node, currentModule, includes) {
  const lines = [];
  if (node.description) lines.push(`/** ${node.description} */`);
  lines.push(`struct ${name} {`);
  const required = new Set(node.required ?? []);
  for (const [property, child] of Object.entries(node.properties ?? {})) {
    if (child.description) lines.push(`  /** ${child.description} */`);
    const type = cppValueType(child, currentModule, includes);
    const optional = !required.has(property) && !typeOf(child).nullable;
    lines.push(`  ${optional ? `std::optional<${type}>` : type} ${property}{};`);
  }
  lines.push('};');
  return lines.join('\n');
}

/** 对象节点的读取与写出函数实现。 */
function cppCodec(name, node, currentModule) {
  const required = new Set(node.required ?? []);
  const reads = [];
  const writes = [];
  for (const [property, child] of Object.entries(node.properties ?? {})) {
    const key = cppString(property);
    const path = `path + ".${property}"`;
    const read = cppReadNullable(child, '*field', path, currentModule);
    const isNullable = typeOf(child).nullable;
    if (required.has(property)) {
      reads.push(`  {\n    const nlohmann::json* field = json_fields::find(json, ${key});\n    if (field == nullptr) throw ::ember::shared::SchemaError(${path} + " 缺少");\n    value.${property} = ${read};\n  }`);
      writes.push(isNullable ? `  json[${key}] = value.${property} ? nlohmann::json(*value.${property}) : nlohmann::json(nullptr);` : `  json[${key}] = value.${property};`);
    } else if (isNullable) {
      reads.push(`  {\n    const nlohmann::json* field = json_fields::find(json, ${key});\n    if (field != nullptr) value.${property} = ${read};\n  }`);
      writes.push(`  if (value.${property}) json[${key}] = *value.${property};`);
    } else {
      reads.push(`  {\n    const nlohmann::json* field = json_fields::find(json, ${key});\n    if (field != nullptr && !field->is_null()) value.${property} = ${cppReadExpr(child, '*field', path, currentModule)};\n  }`);
      writes.push(`  if (value.${property}) json[${key}] = *value.${property};`);
    }
  }
  const known = Object.keys(node.properties ?? {}).map(cppString).join(', ');
  return [
    `${name} parse${name}(const nlohmann::json& json, const std::string& path) {`,
    '  json_fields::requireObject(json, path);',
    `  json_fields::rejectUnknown(json, path, {${known}});`,
    `  ${name} value;`,
    ...reads,
    '  return value;',
    '}',
    '',
    `void to_json(nlohmann::json& json, const ${name}& value) {`,
    '  json = nlohmann::json::object();',
    ...(writes.length === 0 ? ['  static_cast<void>(value);'] : writes),
    '}'
  ].join('\n');
}

/** 生成一个模块的 C++ 头文件与源文件。 */
function generateCpp(module) {
  const { name, schema, file } = module;
  const prefix = pascal(name);
  const constant = snake(name).toUpperCase();
  const namespace = `ember::contracts::${snake(name)}`;
  const includes = new Set();
  const objects = [];
  for (const [typeName, node] of Object.entries(schema.types ?? {})) objects.push([typeName, node]);
  for (const [operation, request] of Object.entries(schema.requests ?? {})) {
    objects.push([`${prefix}${pascal(operation)}Params`, request.params]);
    objects.push([`${prefix}${pascal(operation)}Result`, request.result]);
  }
  for (const [event, definition] of Object.entries(schema.events ?? {})) objects.push([`${prefix}${pascal(event)}Data`, definition.data]);
  const structs = objects.map(([typeName, node]) => cppStruct(typeName, node, name, includes));
  const declarations = objects.map(
    ([typeName]) => `/** 读取并校验 ${typeName}；不合格时抛出 SchemaError。 */\n${typeName} parse${typeName}(const nlohmann::json& json, const std::string& path);\nvoid to_json(nlohmann::json& json, const ${typeName}& value);`
  );
  const requestConstants = Object.keys(schema.requests ?? {}).map(
    (operation) => `/** 请求名：${name}.${operation}。 */\ninline constexpr std::string_view ${constant}_${snake(operation).toUpperCase()}_REQUEST = "${name}.${operation}";`
  );
  const eventConstants = Object.keys(schema.events ?? {}).map(
    (event) => `/** 事件名：${name}.${event}。 */\ninline constexpr std::string_view ${constant}_${snake(event).toUpperCase()}_EVENT = "${name}.${event}";`
  );
  const includeLines = [...includes].toSorted().map((target) => `#include "contracts/${target}/generated/${target}.hpp"`);
  const header = [
    CPP_HEADER(file),
    '#pragma once',
    '',
    '#include <cstdint>',
    '#include <optional>',
    '#include <string>',
    '#include <string_view>',
    '#include <vector>',
    '',
    '#include <nlohmann/json.hpp>',
    ...includeLines,
    '',
    `namespace ${namespace} {`,
    '',
    ...requestConstants,
    ...eventConstants,
    '',
    structs.join('\n\n'),
    '',
    declarations.join('\n\n'),
    '',
    `}  // namespace ${namespace}`,
    ''
  ].join('\n');
  const source = [
    CPP_HEADER(file),
    `#include "contracts/${name}/generated/${name}.hpp"`,
    '',
    '#include "shared/json/json-fields.hpp"',
    '',
    `namespace ${namespace} {`,
    '',
    'namespace json_fields = ::ember::shared::json_fields;',
    '',
    objects.map(([typeName, node]) => cppCodec(typeName, node, name)).join('\n\n'),
    '',
    `}  // namespace ${namespace}`,
    ''
  ].join('\n');
  return { header, source };
}

// ---------------------------------------------------------------- 输出

/** 内容有变化时才写入。 */
function writeIfChanged(path, content, written) {
  if (existsSync(path) && readFileSync(path, 'utf8') === content) return;
  mkdirSync(dirname(path), { recursive: true });
  writeFileSync(path, content, 'utf8');
  written.push(path);
}

/** 删除已不存在的模块的生成目录。 */
function removeStale(baseDir, modules, removed) {
  if (!existsSync(baseDir)) return;
  for (const name of readdirSync(baseDir)) {
    const generated = join(baseDir, name, 'generated');
    if (name === 'core-gateway' || modules.has(name) || !existsSync(generated)) continue;
    rmSync(generated, { recursive: true, force: true });
    removed.push(generated);
  }
}

/**
 * 生成全部代码。
 * @param {string} root 项目根目录。
 * @param {{ debugClient?: { documentFile: string, typesFile: string } }} [options] debugClient：调试客户端用的接口文档与类型文件的路径（VS Code 扩展里），所在的扩展目录存在时才写入。
 * @returns {{ written: string[], removed: string[], modules: string[] }} 写入与删除的文件。
 */
export function generate(root, options = {}) {
  const modules = loadSchemas(root);
  const written = [];
  const removed = [];
  for (const module of modules) {
    const cpp = generateCpp(module);
    writeIfChanged(join(root, 'core', 'src', 'contracts', module.name, 'generated', `${module.name}.hpp`), cpp.header, written);
    writeIfChanged(join(root, 'core', 'src', 'contracts', module.name, 'generated', `${module.name}.cpp`), cpp.source, written);
  }
  // 原生外壳的请求目录：目录存在时才写入（win32/app 不在时跳过）。
  if (existsSync(join(root, 'win32', 'app'))) {
    writeIfChanged(join(root, 'win32', 'app', 'src', 'contracts', 'core-gateway', 'generated', 'request-catalog.hpp'), generateAppRequestCatalog(modules), written);
  }
  // 调试服务的接口描述文档（OpenRPC）：生成核心 debug 模块里的 C++ 常量，并写到 VS Code 扩展里（帮助文档里的协议参考由这份文档生成）。
  if (modules.some((module) => module.name === 'debug')) {
    const document = generateOpenRpc(modules);
    const cpp = generateOpenRpcCpp(document, CPP_HEADER('schema/*.schema.json'));
    writeIfChanged(join(root, 'core', 'src', 'contracts', 'debug', 'generated', 'openrpc.hpp'), cpp.header, written);
    writeIfChanged(join(root, 'core', 'src', 'contracts', 'debug', 'generated', 'openrpc.cpp'), cpp.source, written);
    // VS Code 扩展里的调试客户端：接口文档与类型写到给定的位置，扩展目录不存在时跳过。
    const { debugClient } = options;
    if (debugClient !== undefined && existsSync(dirname(dirname(debugClient.documentFile)))) {
      writeIfChanged(debugClient.documentFile, `${JSON.stringify(document, null, 2)}\n`, written);
      writeIfChanged(debugClient.typesFile, generateSdkTypes(document), written);
    }
  }
  const names = new Set(modules.map((module) => module.name));
  removeStale(join(root, 'core', 'src', 'contracts'), names, removed);
  return { written, removed, modules: [...names] };
}

if (import.meta.url === pathToFileURL(process.argv[1] ?? '').href) {
  const root = resolve(dirname(fileURLToPath(import.meta.url)), '..', '..');
  try {
    const extension = join(root, '..', 'vscode');
    const result = generate(root, {
      debugClient: {
        documentFile: join(extension, 'debug', 'openrpc.json'),
        typesFile: join(extension, 'src', 'contracts', 'debug-client', 'generated', 'api.ts')
      }
    });
    console.log(`Schema 代码生成：${result.modules.length} 个模块，写入 ${result.written.length} 个文件，删除 ${result.removed.length} 个目录。`);
  } catch (error) {
    console.error(error instanceof Error ? error.message : String(error));
    process.exit(1);
  }
}
