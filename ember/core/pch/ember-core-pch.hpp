/*
 * ------------------------------------------------------------------------
 * 名称：ember-core-pch.hpp
 * 说明：核心库的预编译头，只放各处都会用到的标准库头文件，加快编译。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-20
 * 备注：由 core/CMakeLists.txt 的 target_precompile_headers 强制包含；不放第三方库与系统头文件，
 *       各源文件仍要自己包含用到的头文件。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>
