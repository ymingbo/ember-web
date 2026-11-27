/*
 * ------------------------------------------------------------------------
 * 名称：module-list.cpp
 * 说明：C++ 模块清单：每个模块引用自己的登记函数并在清单中占一项；摘除模块时删除这两处。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-13
 * 备注：清单须包含 schema/ 下的全部模块，由 tools/check/check-modules.mjs 检查。
 * ------------------------------------------------------------------------
 */

#include "bootstrap/module-list.hpp"

#include "bootstrap/modules/cartridges.hpp"
#include "bootstrap/modules/debug.hpp"
#include "bootstrap/modules/filemanager.hpp"
#include "bootstrap/modules/input.hpp"
#include "bootstrap/modules/inspector.hpp"
#include "bootstrap/modules/machines.hpp"
#include "bootstrap/modules/mappers.hpp"
#include "bootstrap/modules/media.hpp"
#include "bootstrap/modules/output.hpp"
#include "bootstrap/modules/preferences.hpp"
#include "bootstrap/modules/runtime.hpp"
#include "bootstrap/modules/system.hpp"

namespace ember::bootstrap {

namespace {

// 取用共享服务的模块排在提供方之后。
constexpr ModuleRegistration MODULES[] = {
    {"system", registerSystem},
    {"preferences", registerPreferences},
    {"mappers", registerMappers},
    {"output", registerOutput},
    {"runtime", registerRuntime},
    {"input", registerInput},
    {"inspector", registerInspector},
    {"cartridges", registerCartridges},
    {"machines", registerMachines},
    {"media", registerMedia},
    {"filemanager", registerFileManager},
    {"debug", registerDebug},
};

}  // namespace

std::span<const ModuleRegistration> moduleList() { return MODULES; }

}  // namespace ember::bootstrap
