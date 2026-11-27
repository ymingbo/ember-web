/*
 * ------------------------------------------------------------------------
 * 名称：machine-catalog.cpp
 * 说明：主机型号目录的实现。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * ------------------------------------------------------------------------
 */

#include "bootstrap/machines/machine-catalog.hpp"

#include "bootstrap/machines/bbg/bbg.hpp"
#include "bootstrap/machines/cec1/cec1.hpp"
#include "bootstrap/machines/fc/fc.hpp"
#include "bootstrap/machines/pyramid/pyramid.hpp"
#include "bootstrap/machines/sb2000/sb2000.hpp"
#include "bootstrap/machines/sb97/sb97.hpp"
#include "bootstrap/machines/yuxing/yuxing.hpp"
#include "contracts/runtime/runtime-error.hpp"
#include "shared/errors/core-error.hpp"

namespace ember::bootstrap {

std::unique_ptr<contracts::hardware::Machine> MachineCatalog::create(const std::string& host) const {
  if (host == "fc") return std::make_unique<FcMachine>(tapeDeck_);
  throw shared::CoreError(contracts::runtime::ERROR_HOST_UNSUPPORTED, "程序里还没有实现主机型号：" + host);
}

std::unique_ptr<contracts::hardware::Machine> MachineCatalog::createWithRom(const std::string& machine, std::vector<std::uint8_t> rom) const {
  if (machine == "sb2000") return std::make_unique<Sb2000Machine>(std::move(rom), printerOutput_.get(), floppyActivity_.get(), floppyDrive_, tapeDeck_);
  if (machine == "sb97") return std::make_unique<Sb97Machine>(std::move(rom), floppyActivity_.get(), floppyDrive_, tapeDeck_);
  if (machine == "bbg1") return std::make_unique<BbgMachine>(BbgVariant::floppyOne(), std::move(rom), printerOutput_.get(), floppyActivity_.get(), floppyDrive_, tapeDeck_);
  if (machine == "bbg98") return std::make_unique<BbgMachine>(BbgVariant::model98(), std::move(rom), printerOutput_.get(), floppyActivity_.get(), floppyDrive_, tapeDeck_);
  if (machine == "pec9588") return std::make_unique<PyramidMachine>(PyramidBoard::Model::Pec9588, std::move(rom), tapeDeck_, printerOutput_.get(), floppyActivity_.get(), floppyDrive_);
  if (machine == "pec586") return std::make_unique<PyramidMachine>(PyramidBoard::Model::Pec586, std::move(rom), tapeDeck_, printerOutput_.get());
  if (machine == "yuxing") return std::make_unique<YuxingMachine>(std::move(rom), tapeDeck_, floppyActivity_.get(), floppyDrive_, printerOutput_.get());
  if (machine == "cec1") return std::make_unique<Cec1Machine>(std::move(rom), tapeDeck_, floppyActivity_.get(), floppyDrive_, printerOutput_.get());
  throw shared::CoreError(contracts::runtime::ERROR_HOST_UNSUPPORTED, "程序里还没有实现整机机型：" + machine);
}

}  // namespace ember::bootstrap
