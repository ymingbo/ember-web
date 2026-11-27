/*
 * ------------------------------------------------------------------------
 * 名称：preferences.cpp
 * 说明：首选项模块的登记：创建首选项服务，登记界面类设置项，登记 preferences.* 请求，并把服务提供给其他模块。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * 备注：其他模块（如 output）取用 PreferenceService 登记自己的设置项，须排在本模块之后登记；分组按登记的顺序显示：外观、画面在这里，声音在 output 里。
 * ------------------------------------------------------------------------
 */

#include "bootstrap/modules/preferences.hpp"

#include <memory>

#include "infra/preferences/preference-registry.hpp"
#include "requests/preferences/preferences-requests.hpp"

namespace ember::bootstrap {

namespace api = contracts::preferences;

void registerInterfaceSettings(api::PreferenceService& service) {
  service.addGroup({"appearance", "外观"});
  service.addSetting({.id = "appearance.theme",
                      .group = "appearance",
                      .label = "主题",
                      .description = "跟随系统时，系统切换深色或浅色后立即生效。",
                      .kind = api::SettingKind::Choice,
                      .options = {{"system", "跟随系统"}, {"dark", "深色"}, {"light", "浅色"}},
                      .defaultValue = std::string("system")});

  service.addGroup({"display", "画面"});
  service.addSetting({.id = "display.aspect",
                      .group = "display",
                      .label = "画面比例",
                      .description = "只影响显示，不影响模拟。",
                      .kind = api::SettingKind::Choice,
                      .options = {{"8:7", "8:7（像素原始比例）"}, {"4:3", "4:3（电视）"}},
                      .defaultValue = std::string("8:7")});
  service.addSetting({.id = "display.scaling",
                      .group = "display",
                      .label = "缩放方式",
                      .description = "整数倍缩放能保持像素清晰，窗口放不下时自动取最大的整数倍。",
                      .kind = api::SettingKind::Choice,
                      .options = {{"fit", "适应窗口"}, {"integer", "整数倍"}},
                      .defaultValue = std::string("fit")});
  service.addSetting({.id = "display.filter",
                      .group = "display",
                      .label = "画面效果",
                      .description = "像素让每个像素都清晰分明，平滑让放大后的画面边缘柔和；整数倍缩放时两者相同。",
                      .kind = api::SettingKind::Choice,
                      .options = {{"pixel", "像素"}, {"smooth", "平滑"}},
                      .defaultValue = std::string("pixel")});
  service.addSetting({.id = "display.showAllLines",
                      .group = "display",
                      .label = "显示全部 240 行",
                      .description = "关闭时裁掉上下各 8 行。",
                      .kind = api::SettingKind::Switch,
                      .defaultValue = true});
  service.addSetting({.id = "display.fullScreen",
                      .group = "display",
                      .label = "全屏时的画面",
                      .description = "保持比例时按上面的画面比例与缩放方式显示；拉伸铺满会让画面铺满整个显示器，比例可能与显示器不符而变形。只在全屏时生效，窗口模式不受影响。",
                      .kind = api::SettingKind::Choice,
                      .options = {{"keep", "保持比例"}, {"stretch", "拉伸铺满"}},
                      .defaultValue = std::string("keep")});

  service.addGroup({"update", "更新"});
  service.addSetting({.id = "update.autoCheck",
                      .group = "update",
                      .label = "启动时检查更新",
                      .description = "有新版本时自动下载，下载好后在“帮助”菜单里选择“重启并安装更新”；也可以随时在“帮助”菜单里手动检查。",
                      .kind = api::SettingKind::Switch,
                      .defaultValue = true});

  service.addGroup({"app", "应用"});
  service.addSetting({.id = "app.allowMultipleInstances",
                      .group = "app",
                      .label = "允许同时运行多个实例",
                      .description = "开启后可以再打开新的 Ember 窗口，各实例共用同一份设置，后保存的会覆盖先保存的；调试服务同一时间只有一个实例能使用端口。下次启动时生效。",
                      .kind = api::SettingKind::Switch,
                      .defaultValue = false});
}

void registerPreferences(ModuleRegistry& registry) {
  infra::preferences::PreferenceRegistryOptions options;
  options.file = registry.userDataPath / "config" / "preferences.json";
  options.events = registry.events;
  options.errors = registry.errors;
  auto service = std::make_shared<infra::preferences::PreferenceRegistry>(std::move(options));
  registerInterfaceSettings(*service);
  registry.services.provide<api::PreferenceService>(service);
  requests::preferences::addPreferencesRequests(registry.requests, service);
}

}  // namespace ember::bootstrap
