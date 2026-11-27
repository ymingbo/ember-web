// Emscripten 胶水代码的类型声明（产物由 build-wasm.sh 生成到同目录的 ember_wasm.js）

import type { EmberWasmModule } from '../types/ember';

/** Emscripten 工厂的配置项。 */
export interface EmberModuleConfig {
  /** 定位 .wasm / .data 等附属文件。 */
  locateFile?: (path: string, prefix?: string) => string;
}

declare const moduleFactory: (config?: EmberModuleConfig) => Promise<EmberWasmModule>;
export default moduleFactory;
