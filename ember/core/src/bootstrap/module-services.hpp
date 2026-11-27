/*
 * ------------------------------------------------------------------------
 * 名称：module-services.hpp
 * 说明：模块之间共享的服务：模块登记时把其他模块需要的契约接口放进来，后登记的模块按接口类型取用。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-13
 * 备注：只在组装根中使用；取用方必须排在提供方之后登记（见 module-list.cpp），取不到视为程序错误。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <typeindex>

namespace ember::bootstrap {

/** 模块之间共享的服务，按接口类型登记与取用。 */
class ModuleServices {
 public:
  /** 登记一个契约接口的实现；同一接口重复登记视为程序错误。 */
  template <typename Contract>
  void provide(std::shared_ptr<Contract> service) {
    if (!services_.emplace(std::type_index(typeid(Contract)), std::move(service)).second) throw std::logic_error(std::string("服务重复登记：") + typeid(Contract).name());
  }

  /** 取用一个契约接口；没有登记时抛出 std::logic_error。 */
  template <typename Contract>
  [[nodiscard]] std::shared_ptr<Contract> require() const {
    const auto found = services_.find(std::type_index(typeid(Contract)));
    if (found == services_.end()) throw std::logic_error(std::string("服务尚未登记：") + typeid(Contract).name());
    return std::static_pointer_cast<Contract>(found->second);
  }

 private:
  std::map<std::type_index, std::shared_ptr<void>> services_;
};

}  // namespace ember::bootstrap
