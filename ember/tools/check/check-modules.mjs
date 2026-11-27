/*
 * ------------------------------------------------------------------------
 * 名称：check-modules.mjs
 * 说明：分层与模块检查：C++ 各层的 #include 规则、硬件契约与硬件模块、mapper 电路的规则（模块之间互不引用）；
 *       从 C++ 的 #include 与 Schema 的 $ref 分析模块之间的依赖，发现引用其他模块契约以外的目录与模块循环依赖；
 *       检查模块目录都在模块清单中登记；检查官方插件目录 plugins/<id>/：目录名等于清单里的 id，只含 manifest.json、main.js、icon.png；
 *       检查 Win32 原生应用 win32/app/src 的 C++ 分层（八层、模块只经对方契约协作、Windows 系统头文件只能在 platform 与 bootstrap）。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-13
 * 备注：规则来自 docs/01-软件架构设计.md 与 docs/06-构建与模块登记.md；测试文件不参与检查。
 *       用法：node tools/check/check-modules.mjs [--dependents <模块>]，后者列出直接与间接依赖该模块的全部模块；
 *       checkProject(root) 可被测试引用，对任意目录做同样的检查。
 * ------------------------------------------------------------------------
 */

import { existsSync, readdirSync, readFileSync, statSync } from 'node:fs';
import { dirname, join, relative, resolve } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { loadSchemas } from '../schema-gen/schema-gen.mjs';

/** C++ 按模块分目录的层。 */
const CPP_MODULE_LAYERS = new Set(['contracts', 'logic', 'infra', 'requests']);
/** C++ 各层可以包含的层。 */
const CPP_ALLOWED = {
  contracts: ['contracts', 'shared'],
  logic: ['logic', 'contracts', 'shared', 'platform'],
  infra: ['infra', 'contracts', 'shared', 'platform'],
  requests: ['requests', 'contracts', 'shared', 'platform'],
  platform: ['platform', 'shared'],
  shared: ['shared'],
  bootstrap: ['contracts', 'logic', 'infra', 'requests', 'platform', 'shared', 'bootstrap']
};
/** logic 层只能包含的 platform 抽象：事件、取消、错误日志、帧输出、声音输出、文件监视。 */
const PLATFORM_ABSTRACTIONS = new Set([
  'platform/events/event-sink.hpp',
  'platform/requests/cancellation.hpp',
  'platform/logging/error-log.hpp',
  'platform/streams/frame-output.hpp',
  'platform/audio/audio-output.hpp',
  'platform/files/file-watcher.hpp'
]);
/** 硬件模块只能包含的 platform 抽象：日志、画面输出与声音输出不属于硬件（声音经主机的契约取走）。 */
const HARDWARE_PLATFORM_ABSTRACTIONS = new Set(['platform/events/event-sink.hpp', 'platform/requests/cancellation.hpp']);
/**
 * 硬件目录：contracts/hardware 是硬件契约，logic/hardware/<模块> 是硬件模块（一个型号一个目录）。
 * 硬件不是业务模块，没有请求，所以不要求登记在模块清单中；硬件模块之间互不引用，只经 contracts/hardware。
 */
const HARDWARE_DIRECTORY = 'hardware';
/** 硬件契约的目录前缀（#include 路径与相对 core/src 的文件路径都以它开头）。 */
const HARDWARE_CONTRACTS_PREFIX = 'contracts/hardware/';
/** 硬件模块的目录前缀。 */
const HARDWARE_LOGIC_PREFIX = 'logic/hardware/';
/**
 * mapper 电路（MMC3 等，可装在卡带上，也可在学习机主板上）：logic/hardware/mapper-chips/<芯片> 一个芯片一个目录，是硬件模块的一类；
 * 规则同硬件模块：不要求登记在模块清单中，芯片之间互不引用，只经 contracts/hardware。
 */
const MAPPER_CHIP_DIRECTORY = 'mapper-chips';
/** 标准库头文件：没有扩展名与目录的名字（如 <cstdint>、<span>）。 */
const STANDARD_HEADER = /^[a-z][a-z0-9_]*$/;
/**
 * 主机型号的装配：bootstrap/machines/<机型>/ 一个机型一个目录，多个机型共用的放 bootstrap/machines/common/；
 * machines 目录下直接放的只有按型号名创建主机的 machine-catalog。
 */
const MACHINES_PREFIX = 'bootstrap/machines/';
const MACHINES_COMMON = 'common';
const MACHINE_CATALOG = /^machine-catalog\.(cpp|hpp)$/;
/** 机型文件可以包含的前缀：硬件契约、硬件模块与 shared。 */
const MACHINE_ALLOWED_PREFIXES = [HARDWARE_CONTRACTS_PREFIX, HARDWARE_LOGIC_PREFIX, 'shared/'];

/** 递归列出文件。 */
function listFiles(directory, pattern) {
  if (!existsSync(directory)) return [];
  const result = [];
  for (const name of readdirSync(directory)) {
    const path = join(directory, name);
    if (statSync(path).isDirectory()) result.push(...listFiles(path, pattern));
    else if (pattern.test(name)) result.push(path);
  }
  return result;
}

/** 列出子目录名。 */
function listDirectories(directory) {
  if (!existsSync(directory)) return [];
  return readdirSync(directory).filter((name) => statSync(join(directory, name)).isDirectory());
}

/** 统一为以 / 分隔的相对路径。 */
function toPosix(path) {
  return path.split('\\').join('/');
}

/** 找出有向图中的环。 */
function findCycles(graph) {
  const cycles = [];
  const state = new Map();
  const stack = [];
  const visit = (node) => {
    state.set(node, 'visiting');
    stack.push(node);
    for (const next of graph.get(node) ?? []) {
      if (state.get(next) === 'visiting') cycles.push([...stack.slice(stack.indexOf(next)), next].join(' → '));
      else if (!state.has(next)) visit(next);
    }
    stack.pop();
    state.set(node, 'done');
  };
  for (const node of graph.keys()) if (!state.has(node)) visit(node);
  return cycles;
}

/** 由边列表建图。 */
function toGraph(edges) {
  const graph = new Map();
  for (const edge of edges) {
    if (!graph.has(edge.from)) graph.set(edge.from, new Set());
    graph.get(edge.from).add(edge.to);
  }
  return graph;
}

// ---------------------------------------------------------------- C++

/**
 * C++ 文件所在的层与模块：硬件契约属于模块 hardware，每个硬件模块自成一个模块 hardware/<名称>，
 * 这样硬件模块之间的引用与业务模块之间的引用走同一套“只能引用对方契约”的规则。
 */
function cppLocation(path) {
  const parts = path.split('/');
  const layer = parts[0];
  if (CPP_MODULE_LAYERS.has(layer) && parts.length > 2) {
    if (layer === 'logic' && parts[1] === HARDWARE_DIRECTORY && parts.length > 3) {
      if (parts[2] === MAPPER_CHIP_DIRECTORY && parts.length > 4) return { layer, module: `${HARDWARE_DIRECTORY}/${MAPPER_CHIP_DIRECTORY}/${parts[3]}` };
      return { layer, module: `${parts[1]}/${parts[2]}` };
    }
    return { layer, module: parts[1] };
  }
  if (layer === 'bootstrap' && parts[1] === 'modules' && parts.length === 3) return { layer, module: parts[2].replace(/\.(cpp|hpp)$/, '') };
  return { layer, module: undefined };
}

/** 第三方头文件可以出现的位置。 */
function thirdPartyAllowed(include, path, location) {
  if (include.startsWith('nlohmann/')) {
    if (location.layer === 'contracts') return path.includes('/generated/');
    if (location.layer === 'shared') return path.startsWith('shared/json/');
    return ['platform', 'infra', 'requests', 'bootstrap'].includes(location.layer);
  }
  if (include === 'quickjs.h') return path.startsWith('infra/mappers/');
  if (include === 'miniz.h') return location.layer === 'infra';
  if (include === 'miniaudio.h') return path.startsWith('platform/audio/');
  if (include.startsWith('libavcodec/') || include.startsWith('libavformat/') || include.startsWith('libavutil/') || include.startsWith('libswresample/')) return path.startsWith('infra/media/');
  return true;
}

/** 机型装配里的文件所属的机型目录（common 也算一个）；直接放在 machines 下的文件 name 为空，machine-catalog 不属于任何机型。 */
function machineOf(path) {
  if (!path.startsWith(MACHINES_PREFIX)) return undefined;
  const parts = path.slice(MACHINES_PREFIX.length).split('/');
  if (parts.length === 1) return MACHINE_CATALOG.test(parts[0]) ? undefined : { name: undefined };
  return { name: parts[0] };
}

/** 检查一个机型文件的一条引用的包含路径，有问题时返回说明。 */
function machineIncludeProblem(machine, include) {
  if (include.startsWith(MACHINES_PREFIX)) {
    const target = include.slice(MACHINES_PREFIX.length).split('/');
    const targetName = target.length === 1 ? undefined : target[0];
    if (targetName !== machine.name && targetName !== MACHINES_COMMON) return `机型 ${machine.name} 只能包含自己目录与 ${MACHINES_COMMON} 里的文件，不能包含 ${include}`;
    return undefined;
  }
  if (HARDWARE_PLATFORM_ABSTRACTIONS.has(include) || MACHINE_ALLOWED_PREFIXES.some((prefix) => include.startsWith(prefix))) return undefined;
  return `机型 ${machine.name} 只能包含硬件契约、硬件模块、shared 与 platform 的抽象，不能包含 ${include}`;
}

/** 检查 C++ 的 #include，并收集模块之间的依赖。 */
function checkCpp(root, errors, edges) {
  const base = join(root, 'core', 'src');
  for (const file of listFiles(base, /\.(cpp|hpp)$/)) {
    const path = toPosix(relative(base, file));
    const from = cppLocation(path);
    const machine = machineOf(path);
    if (machine !== undefined && machine.name === undefined) errors.push(`core/src/${path}：机型的文件必须放在 bootstrap/machines/<机型>/ 或 bootstrap/machines/${MACHINES_COMMON}/ 目录下`);
    const inHardwareContracts = path.startsWith(HARDWARE_CONTRACTS_PREFIX);
    const inHardwareLogic = path.startsWith(HARDWARE_LOGIC_PREFIX);
    if (inHardwareLogic) {
      const isMapperChip = path.startsWith(`${HARDWARE_LOGIC_PREFIX}${MAPPER_CHIP_DIRECTORY}/`);
      if (isMapperChip && path.split('/').length < 5) errors.push(`core/src/${path}：mapper 芯片的文件必须放在 logic/hardware/mapper-chips/<芯片>/ 目录下`);
      else if (!isMapperChip && path.split('/').length < 4) errors.push(`core/src/${path}：硬件模块的文件必须放在 logic/hardware/<模块>/ 目录下`);
    }
    const text = readFileSync(file, 'utf8');
    for (const match of text.matchAll(/^\s*#include\s+([<"])([^>"]+)[>"]/gm)) {
      const include = match[2];
      if (match[1] === '<') {
        if (inHardwareContracts) {
          if (!STANDARD_HEADER.test(include)) errors.push(`core/src/${path}：contracts/hardware 只能包含标准库头文件，不能包含 <${include}>`);
          continue;
        }
        if (!thirdPartyAllowed(include, path, from)) errors.push(`core/src/${path}：不能包含 <${include}>`);
        continue;
      }
      if (inHardwareContracts) {
        if (!include.startsWith(HARDWARE_CONTRACTS_PREFIX) && !include.startsWith('shared/')) errors.push(`core/src/${path}：contracts/hardware 只能包含 contracts/hardware 与 shared，不能包含 ${include}`);
        continue;
      }
      const to = cppLocation(include);
      if (machine !== undefined && machine.name !== undefined) {
        const problem = machineIncludeProblem(machine, include);
        if (problem !== undefined) errors.push(`core/src/${path}：${problem}`);
      }
      const allowed = CPP_ALLOWED[from.layer];
      if (allowed !== undefined && !allowed.includes(to.layer)) errors.push(`core/src/${path}：${from.layer} 层不能包含 ${to.layer} 层的 ${include}`);
      if (from.layer === 'logic' && to.layer === 'platform' && !(inHardwareLogic ? HARDWARE_PLATFORM_ABSTRACTIONS : PLATFORM_ABSTRACTIONS).has(include)) {
        errors.push(`core/src/${path}：logic 层只能包含 platform 的抽象，不能包含 ${include}`);
      }
      if (inHardwareLogic && to.layer === 'contracts' && !include.startsWith(HARDWARE_CONTRACTS_PREFIX)) errors.push(`core/src/${path}：硬件模块只能包含 contracts/hardware 的契约，不能包含 ${include}`);
      if (from.module !== undefined && to.module !== undefined && from.module !== to.module) {
        if (to.layer !== 'contracts') errors.push(`core/src/${path}：模块 ${from.module} 只能引用模块 ${to.module} 的契约，不能包含 ${include}`);
        edges.push({ from: from.module, to: to.module });
      }
    }
  }
}

/** 直接与间接依赖 target 的模块。 */
function dependentsOf(target, edges) {
  const result = new Set();
  const queue = [target];
  while (queue.length > 0) {
    const current = queue.shift();
    for (const edge of edges) {
      if (edge.to === current && !result.has(edge.from) && edge.from !== target) {
        result.add(edge.from);
        queue.push(edge.from);
      }
    }
  }
  return [...result].toSorted();
}

// ---------------------------------------------------------------- 登记

/** 检查模块目录都在 C++ 模块清单中登记。 */
function checkRegistration(root, errors, schemaModules) {
  const cppList = join(root, 'core', 'src', 'bootstrap', 'module-list.cpp');
  const cppText = existsSync(cppList) ? readFileSync(cppList, 'utf8') : '';
  const cppModules = new Set([...cppText.matchAll(/\{\s*"([a-z0-9-]+)"\s*,/g)].map((match) => match[1]));
  const requireIn = (module, reason) => {
    if (!cppModules.has(module)) errors.push(`模块 ${module}${reason}，但没有登记在 core/src/bootstrap/module-list.cpp`);
  };
  for (const module of schemaModules) requireIn(module, ' 有 Schema');
  for (const layer of CPP_MODULE_LAYERS) {
    for (const module of listDirectories(join(root, 'core', 'src', layer))) {
      if (module === HARDWARE_DIRECTORY) continue;
      requireIn(module, ` 有 core/src/${layer} 目录`);
    }
  }
  for (const module of cppModules) {
    if (!existsSync(join(root, 'core', 'src', 'bootstrap', 'modules', `${module}.cpp`))) errors.push(`模块 ${module} 登记在 C++ 模块清单中，但没有 core/src/bootstrap/modules/${module}.cpp`);
  }
}

// ---------------------------------------------------------------- Win32 原生应用

/** 原生应用的八层，按模块分目录的层。 */
const APP_LAYERS = new Set(['contracts', 'logic', 'infra', 'entry', 'ui', 'platform', 'shared', 'bootstrap']);
const APP_MODULE_LAYERS = new Set(['contracts', 'logic', 'infra', 'entry', 'ui']);
/** 原生应用各层可以包含的层。 */
const APP_ALLOWED = {
  contracts: ['contracts', 'shared'],
  logic: ['logic', 'contracts', 'shared'],
  infra: ['infra', 'contracts', 'shared', 'platform'],
  entry: ['entry', 'contracts', 'shared'],
  ui: ['ui', 'contracts'],
  platform: ['platform', 'shared'],
  shared: ['shared'],
  bootstrap: [...APP_LAYERS]
};
/** 不属于任何业务模块的目录（请求路由与参数校验），其他模块可以包含。 */
const APP_FRAMEWORK_ENTRY = new Set(['main-requests']);
/** 核心头文件（经 ember_core 的包含目录）只能出现在这些层：基础设施里的核心网关与组装根；允许的核心头文件只有下面两个。 */
const APP_CORE_HEADERS = new Set(['bootstrap/core.hpp', 'platform/streams/frame-output.hpp']);
const APP_CORE_HEADER_LAYERS = new Set(['infra', 'bootstrap']);
/** 控件库的头文件前缀，只有界面层、平台层与组装根可以包含。 */
const APP_UI_KIT_PREFIX = 'rujian/';
const APP_UI_KIT_LAYERS = new Set(['ui', 'platform', 'bootstrap']);
/** Windows 系统头文件：只有平台层与组装根可以包含。 */
const APP_SYSTEM_HEADERS = new Set([
  'windows.h',
  'windowsx.h',
  'shellapi.h',
  'shlobj.h',
  'shobjidl.h',
  'knownfolders.h',
  'uxtheme.h',
  'imm.h',
  'd2d1.h',
  'd3d11.h',
  'dxgi.h',
  'dwrite.h',
  'wrl/client.h'
]);
const APP_SYSTEM_HEADER_LAYERS = new Set(['platform', 'bootstrap']);

/** 原生应用的文件所在的层与模块；请求路由等框架目录没有模块。 */
function appLocation(path) {
  const parts = path.split('/');
  const layer = parts[0];
  if (APP_MODULE_LAYERS.has(layer) && parts.length > 2) {
    if (layer === 'entry' && APP_FRAMEWORK_ENTRY.has(parts[1])) return { layer, module: undefined };
    return { layer, module: parts[1] };
  }
  return { layer, module: undefined };
}

/** 检查 Win32 原生应用的 C++ 分层与模块之间的引用。 */
function checkWin32App(root, errors) {
  const base = join(root, 'win32', 'app', 'src');
  if (!existsSync(base)) return;
  const edges = [];
  for (const directory of listDirectories(base)) {
    if (!APP_LAYERS.has(directory)) errors.push(`win32/app/src/${directory}：不是八层之一（${[...APP_LAYERS].join('、')}）`);
  }
  for (const file of listFiles(base, /\.(cpp|hpp)$/)) {
    const path = toPosix(relative(base, file));
    const from = appLocation(path);
    if (!APP_LAYERS.has(from.layer)) continue;
    if (APP_MODULE_LAYERS.has(from.layer) && path.split('/').length < 3) errors.push(`win32/app/src/${path}：${from.layer} 层的文件必须放在 ${from.layer}/<模块>/ 目录下`);
    const text = readFileSync(file, 'utf8');
    for (const match of text.matchAll(/^\s*#include\s+([<"])([^>"]+)[>"]/gm)) {
      const include = match[2];
      if (match[1] === '<') {
        if (APP_SYSTEM_HEADERS.has(include) && !APP_SYSTEM_HEADER_LAYERS.has(from.layer)) errors.push(`win32/app/src/${path}：${from.layer} 层不能包含系统头文件 <${include}>`);
        continue;
      }
      if (include.startsWith(APP_UI_KIT_PREFIX)) {
        if (!APP_UI_KIT_LAYERS.has(from.layer)) errors.push(`win32/app/src/${path}：${from.layer} 层不能包含控件库的 ${include}`);
        continue;
      }
      // 应用自己的文件优先；不在应用里的只能是登记过的核心头文件
      if (!existsSync(join(base, include)) && !include.startsWith('contracts/core-gateway/generated/')) {
        if (!APP_CORE_HEADERS.has(include)) errors.push(`win32/app/src/${path}：找不到 ${include}，核心的头文件只能包含 ${[...APP_CORE_HEADERS].join('、')}`);
        else if (!APP_CORE_HEADER_LAYERS.has(from.layer)) errors.push(`win32/app/src/${path}：${from.layer} 层不能包含核心的 ${include}`);
        continue;
      }
      const to = appLocation(include);
      if (!APP_LAYERS.has(to.layer)) continue;
      const allowed = APP_ALLOWED[from.layer];
      if (allowed !== undefined && !allowed.includes(to.layer)) errors.push(`win32/app/src/${path}：${from.layer} 层不能包含 ${to.layer} 层的 ${include}`);
      if (from.module !== undefined && to.module !== undefined && from.module !== to.module) {
        if (to.layer !== 'contracts') errors.push(`win32/app/src/${path}：模块 ${from.module} 只能引用模块 ${to.module} 的契约，不能包含 ${include}`);
        edges.push({ from: from.module, to: to.module });
      }
    }
  }
  for (const cycle of findCycles(toGraph(edges))) errors.push(`win32/app：模块之间有循环依赖：${cycle}`);
}

// ---------------------------------------------------------------- 官方插件

/** 插件目录里允许的文件，与插件包的格式一致（docs/03-mapper插件规范.md 第 3 节）。 */
const PLUGIN_FILES = new Set(['manifest.json', 'main.js', 'icon.png']);

/** 检查官方插件目录：只含允许的文件、有清单、清单里的 id 等于目录名。 */
function checkPlugins(root, errors) {
  const pluginsDir = join(root, 'plugins');
  for (const name of listDirectories(pluginsDir)) {
    const directory = join(pluginsDir, name);
    for (const entry of readdirSync(directory)) {
      if (!PLUGIN_FILES.has(entry) || statSync(join(directory, entry)).isDirectory()) errors.push(`plugins/${name}：不允许出现 ${entry}（只允许 ${[...PLUGIN_FILES].join('、')}）`);
    }
    const manifestFile = join(directory, 'manifest.json');
    if (!existsSync(manifestFile)) {
      errors.push(`plugins/${name}：缺少 manifest.json`);
      continue;
    }
    let manifest;
    try {
      manifest = JSON.parse(readFileSync(manifestFile, 'utf8'));
    } catch {
      errors.push(`plugins/${name}/manifest.json 不是合法的 JSON`);
      continue;
    }
    if (manifest?.id !== name) errors.push(`plugins/${name}：manifest.json 的 id 必须等于目录名`);
  }
}

// ---------------------------------------------------------------- 入口

/**
 * 检查一个项目目录。
 * @param {string} root 项目根目录。
 * @returns {{ errors: string[], edges: Array<{ from: string, to: string }>, schemaCount: number }} 错误（已去重）、模块之间的依赖与 Schema 模块数。
 */
export function checkProject(root) {
  const schemas = loadSchemas(root);
  const errors = [];
  const edges = schemas.flatMap((schema) => schema.refs.filter((ref) => ref.module !== schema.name).map((ref) => ({ from: schema.name, to: ref.module })));
  checkCpp(root, errors, edges);
  checkWin32App(root, errors);
  checkPlugins(root, errors);
  for (const cycle of findCycles(toGraph(edges))) errors.push(`模块之间有循环依赖：${cycle}`);
  checkRegistration(
    root,
    errors,
    schemas.map((schema) => schema.name)
  );
  return { errors: [...new Set(errors)], edges, schemaCount: schemas.length };
}

if (import.meta.url === pathToFileURL(process.argv[1] ?? '').href) {
  const root = resolve(dirname(fileURLToPath(import.meta.url)), '..', '..');
  try {
    const { errors, edges, schemaCount } = checkProject(root);
    const index = process.argv.indexOf('--dependents');
    if (index >= 0) {
      const target = process.argv[index + 1];
      const dependents = dependentsOf(target, edges);
      console.log(dependents.length > 0 ? `依赖 ${target} 的模块：${dependents.join('、')}` : `没有模块依赖 ${target}`);
    }
    if (errors.length > 0) {
      for (const error of errors) console.error(error);
      process.exit(1);
    }
    console.log(`分层与模块检查通过：${schemaCount} 个 Schema 模块。`);
  } catch (error) {
    console.error(error instanceof Error ? error.message : String(error));
    process.exit(1);
  }
}
