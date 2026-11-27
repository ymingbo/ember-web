# ------------------------------------------------------------------------
# 名称：CMakeLists.txt
# 说明：Ember FC 模拟器 WebAssembly 版构建：把 ember/core 的核心源码编译成
#       一个 Emscripten 模块（ember_wasm.js + ember_wasm.wasm），再由 ember-web/web
#       里的 React 前端加载。
# 备注：上游 ember/core 目录保持原样，本工程只读取它的 src/ 与 third_party/；
#       Web 版不编译桌面端专属实现（系统音频输出、磁带 FFmpeg 解码、TCP 调试服务、
#       线程池、文件监视等），这些在 Emscripten 下不可用或无意义；
#       核心源码中 src/contracts/*/generated 由 `node tools/schema-gen/schema-gen.mjs` 生成，
#       配置前须先生成。
# ------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.20)
cmake_policy(SET CMP0091 NEW)

project(ember_web LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(EMBER_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(EMBER_CORE ${EMBER_ROOT}/ember/core)
set(EMBER_THIRD_PARTY ${EMBER_CORE}/third_party)
set(EMBER_PLUGINS_SRC ${EMBER_ROOT}/ember/plugins)
# 预加载用的插件目录。PluginRegistry 只认 .emap 打包文件，而上游 ember/plugins/
# 放的是源码目录（nrom/manifest.json + main.js），直接 preload 过去扫描时会被全部跳过，
# 结果注册表为空、加载任何卡带都报 mappers.noMatch。所以构建前先用
# scripts/pack-plugins.py 把源码目录打成 .emap，这里指向打包产物。
set(EMBER_PLUGINS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/build/plugins)
set(EMBER_CORE_VERSION "0.0.0-web" CACHE STRING "核心版本")

if(NOT EMSCRIPTEN)
  message(FATAL_ERROR
    "本工程只能使用 Emscripten 工具链构建。请执行：\n"
    "  emcmake cmake -B build .\n"
    "  emmake make -C build")
endif()

# ---------------------------------------------------------------- 第三方库

add_library(ember_json INTERFACE)
target_include_directories(ember_json SYSTEM INTERFACE ${EMBER_THIRD_PARTY}/nlohmann-json/include)

# mapper 插件的沙箱：只编译引擎核心，不编译提供文件与进程访问的 quickjs-libc。
add_library(ember_quickjs STATIC
  ${EMBER_THIRD_PARTY}/quickjs-ng/quickjs.c
  ${EMBER_THIRD_PARTY}/quickjs-ng/libregexp.c
  ${EMBER_THIRD_PARTY}/quickjs-ng/libunicode.c
  ${EMBER_THIRD_PARTY}/quickjs-ng/dtoa.c)
target_include_directories(ember_quickjs SYSTEM PUBLIC ${EMBER_THIRD_PARTY}/quickjs-ng)
set_target_properties(ember_quickjs PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
target_compile_definitions(ember_quickjs PRIVATE _GNU_SOURCE)
target_compile_options(ember_quickjs PRIVATE -w -funsigned-char)

add_library(ember_miniz STATIC ${EMBER_THIRD_PARTY}/miniz/miniz.c)
target_include_directories(ember_miniz SYSTEM PUBLIC ${EMBER_THIRD_PARTY}/miniz)
target_compile_definitions(ember_miniz PUBLIC MINIZ_NO_ZLIB_COMPATIBLE_NAMES)

# ---------------------------------------------------------------- 核心库

file(GLOB_RECURSE EMBER_CORE_SOURCES CONFIGURE_DEPENDS ${EMBER_CORE}/src/*.cpp)

# 按平台分开的实现：*-win.cpp 只在 Windows 编译，*-mac.cpp 只在 macOS 编译。
if(NOT WIN32)
  list(FILTER EMBER_CORE_SOURCES EXCLUDE REGEX "-win\\.cpp$")
endif()
if(NOT APPLE)
  list(FILTER EMBER_CORE_SOURCES EXCLUDE REGEX "-mac\\.cpp$")
endif()

# Web 版不编译的桌面端实现。
#   miniaudio-output     系统音频输出设备，Web 版改用 Web Audio API
#   disk-tape-store      磁带声音解码，需要 FFmpeg（第三方库里没有 Web 版本）
#   debug-text-file-writer 往本机文件写调试日志
#   tcp-socket/websocket 本机 TCP 调试服务与 WebSocket 服务端
#   thread-pool          线程池，Web 版单线程跑
#   debug-host/server/session  调试服务
#   media-manager        软驱/磁带管理，依赖本机文件与后台线程
#   simulation-runtime   后台模拟线程
#   core + module-list + modules/*  模块装配层（把这些桌面端实现串起来），Web 版直接用 Machine 层
set(EMBER_WEB_EXCLUDED_SOURCES
  ${EMBER_CORE}/src/platform/audio/miniaudio-output.cpp
  ${EMBER_CORE}/src/infra/media/disk-tape-store.cpp
  ${EMBER_CORE}/src/infra/output/debug-text-file-writer.cpp
  ${EMBER_CORE}/src/platform/network/tcp-socket.cpp
  ${EMBER_CORE}/src/platform/network/websocket.cpp
  ${EMBER_CORE}/src/platform/threading/thread-pool.cpp
  ${EMBER_CORE}/src/infra/debug/debug-host.cpp
  ${EMBER_CORE}/src/infra/debug/debug-server.cpp
  ${EMBER_CORE}/src/infra/debug/debug-session.cpp
  ${EMBER_CORE}/src/logic/media/media-manager.cpp
  ${EMBER_CORE}/src/logic/runtime/simulation-runtime.cpp
  ${EMBER_CORE}/src/bootstrap/core.cpp
  ${EMBER_CORE}/src/bootstrap/module-list.cpp
  ${EMBER_CORE}/src/bootstrap/modules/output.cpp
  ${EMBER_CORE}/src/bootstrap/modules/media.cpp
  ${EMBER_CORE}/src/bootstrap/modules/debug.cpp
  ${EMBER_CORE}/src/bootstrap/modules/runtime.cpp)
list(REMOVE_ITEM EMBER_CORE_SOURCES ${EMBER_WEB_EXCLUDED_SOURCES})

add_library(ember_core STATIC ${EMBER_CORE_SOURCES})
target_include_directories(ember_core PUBLIC ${EMBER_CORE}/src)
target_link_libraries(ember_core PUBLIC ember_json ember_miniz PRIVATE ember_quickjs)
target_compile_definitions(ember_core PUBLIC EMBER_CORE_VERSION="${EMBER_CORE_VERSION}")
target_compile_options(ember_core PRIVATE -Wall -Wextra -UPAGE_SIZE -fexceptions)
if(EXISTS ${EMBER_CORE}/pch/ember-core-pch.hpp)
  target_precompile_headers(ember_core PRIVATE ${EMBER_CORE}/pch/ember-core-pch.hpp)
endif()

# ---------------------------------------------------------------- WASM 模块

add_executable(ember_wasm
  ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/web/ember_web_api.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/web/wasm_bindings.cpp)
target_include_directories(ember_wasm PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${EMBER_CORE}/src)
target_link_libraries(ember_wasm PRIVATE ember_core)
target_compile_options(ember_wasm PRIVATE -Wall -Wextra -UPAGE_SIZE -fexceptions)

# mapper 插件随模块一起打包：预加载到虚拟文件系统的 /plugins 目录，
# 与 EmberEmulator 里 options.bundledDirectory = "/plugins" 对应。
if(EXISTS ${EMBER_PLUGINS_SRC})
  # 产物比源目录旧（或不存在）就重新打包，保证源码改了插件能跟上
  execute_process(
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/scripts/pack-plugins.py
            ${EMBER_PLUGINS_SRC} ${EMBER_PLUGINS_DIR}
    RESULT_VARIABLE PACK_RESULT
    OUTPUT_VARIABLE PACK_OUTPUT)
  message(STATUS "${PACK_OUTPUT}")
  if(NOT PACK_RESULT EQUAL 0)
    message(FATAL_ERROR "插件打包失败，见上面的输出")
  endif()
endif()

if(EXISTS ${EMBER_PLUGINS_DIR})
  set(EMBER_PLUGIN_PRELOAD "--preload-file" "${EMBER_PLUGINS_DIR}@/plugins")
  message(STATUS "mapper 插件随包预加载：${EMBER_PLUGINS_DIR} → /plugins")
else()
  # 走到这里说明没打包成功，普通 FC 卡带会全部加载失败，必须显式警告
  message(WARNING "没有插件目录，普通 FC 卡带将无法加载（mappers.noMatch）")
  set(EMBER_PLUGIN_PRELOAD "")
endif()

target_link_options(ember_wasm PRIVATE
  --bind
  -sMODULARIZE=1
  -sEXPORT_ES6=1
  -sEXPORT_NAME=createEmberModule
  -sALLOW_MEMORY_GROWTH=1
  -sEXPORTED_RUNTIME_METHODS=['HEAPU8','HEAPF32']
  -sENVIRONMENT=web,worker
  -sFILESYSTEM=1
  -fexceptions
  -sDISABLE_EXCEPTION_CATCHING=0
  -sSTACK_SIZE=1048576
  -sINITIAL_MEMORY=33554432
  ${EMBER_PLUGIN_PRELOAD})

set_target_properties(ember_wasm PROPERTIES
  OUTPUT_NAME "ember_wasm"
  SUFFIX ".js")
