/*
 * ------------------------------------------------------------------------
 * 名称：ember_web_api.cpp
 * 说明：Ember FC 模拟器 WebAssembly C API 实现：
 *       支持 FC 卡带模式（iNES + mapper 插件）和学习机模式
 *       （裕兴 / 小霸王 / 步步高 / 金字塔等整机机型）。
 *       完整外设支持：键盘、鼠标、软驱、打印机、磁带机。
 * ------------------------------------------------------------------------
 */

#include "platform/web/ember_web_api.hpp"

#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "bootstrap/machines/common/mapper-circuit-catalog.hpp"
#include "bootstrap/machines/fc/fc.hpp"
#include "bootstrap/machines/machine-catalog.hpp"
#include "contracts/cartridges/cartridge-loader.hpp"
#include "contracts/hardware/machine.hpp"
#include "contracts/hardware/printer-output.hpp"
#include "contracts/mappers/mapper-cartridge.hpp"
#include "contracts/mappers/mapper-factory.hpp"
#include "infra/mappers/plugin-registry.hpp"
#include "logic/cartridges/ines-cartridge-loader.hpp"
#include "logic/cartridges/ines-parser.hpp"
#include "logic/hardware/tape-deck/tape-deck.hpp"
#include "shared/errors/core-error.hpp"

namespace {

// ------------------------------------------------------------------
// Web 版打印机输出：捕获最后一页，供 JS 端读取
// ------------------------------------------------------------------
class WebPrinterOutput final : public ember::contracts::hardware::PrinterOutput {
 public:
  void outputPage(ember::contracts::hardware::PrintedPage page) override {
    std::lock_guard<std::mutex> lock(mutex_);
    lastPage_ = std::move(page);
    pagesOutput_++;
  }

  [[nodiscard]] uint64_t pagesOutput() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return pagesOutput_;
  }

  [[nodiscard]] const ember::contracts::hardware::PrintedPage* lastPage() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return lastPage_.rows.empty() ? nullptr : &lastPage_;
  }

 private:
  mutable std::mutex mutex_;
  ember::contracts::hardware::PrintedPage lastPage_;
  uint64_t pagesOutput_{0};
};

// ------------------------------------------------------------------
// Web 版磁带数据源：从内存向量读取
// ------------------------------------------------------------------
class VectorTapeSource final : public ember::contracts::hardware::TapeSource {
 public:
  explicit VectorTapeSource(std::vector<std::int16_t> samples, bool stereo)
      : samples_(std::move(samples)), stereo_(stereo), position_(0) {}

  std::size_t read(std::int16_t* interleaved, std::size_t frames) override {
    const std::size_t channels = stereo_ ? 2 : 1;
    const std::size_t remaining = samples_.size() / channels - position_;
    const std::size_t count = std::min(frames, remaining);
    if (count > 0) {
      std::memcpy(interleaved,
                  samples_.data() + position_ * channels,
                  count * channels * sizeof(std::int16_t));
      position_ += count;
    }
    return count;
  }

  void rewind() override { position_ = 0; }

  void seek(std::uint64_t frame) override {
    const std::size_t channels = stereo_ ? 2 : 1;
    position_ = std::min<std::uint64_t>(frame, samples_.size() / channels);
  }

  [[nodiscard]] double durationSeconds() const override {
    const std::size_t channels = stereo_ ? 2 : 1;
    return static_cast<double>(samples_.size() / channels) /
           static_cast<double>(SAMPLE_RATE);
  }

  [[nodiscard]] bool stereo() const override { return stereo_; }

 private:
  std::vector<std::int16_t> samples_;
  bool stereo_;
  std::size_t position_;
};

}  // namespace

/**
 * 模拟器实例：支持两种模式——
 *   1. FC 卡带模式：FcMachine + iNES 卡带装载器 + mapper 插件注册表
 *   2. 学习机模式：MachineCatalog 创建的整机机型（ROM 在主板上）
 */
struct EmberEmulator {
  /** 主机实例（基类指针，FC 或学习机）。 */
  std::unique_ptr<ember::contracts::hardware::Machine> machine;

  // ---- FC 卡带模式专用 ----
  std::shared_ptr<ember::infra::mappers::PluginRegistry> plugins;
  std::unique_ptr<ember::contracts::cartridges::CartridgeLoader> loader;
  std::unique_ptr<ember::contracts::mappers::MapperCartridge> cartridge;

  // ---- 学习机模式专用 ----
  std::unique_ptr<ember::bootstrap::MachineCatalog> machineCatalog;

  // ---- 外设 ----
  /** 打印机输出接收器（Web 版）。 */
  std::shared_ptr<WebPrinterOutput> printerOutput;

  /** 音频采样缓冲（复用）。 */
  std::vector<float> audioBuffer;
  /** 最后一条错误消息。 */
  std::string lastError;
  bool paused{false};
  uint64_t frameCount{0};
  bool isLearningMachine{false};
};

// ============================================================================
// 内部辅助
// ============================================================================

namespace {

template <typename F>
int catchAll(EmberEmulator* emu, F&& fn) {
  try {
    fn();
    return 0;
  } catch (const ember::shared::CoreError& e) {
    emu->lastError = std::string("CoreError: ") + e.what() + " (" + std::string(e.code()) + ")";
    return 1;
  } catch (const std::exception& e) {
    emu->lastError = std::string("Exception: ") + e.what();
    return 2;
  } catch (...) {
    emu->lastError = "Unknown error";
    return 3;
  }
}

std::string detectMachineType(const ember::contracts::mappers::CartridgeDescription& desc) {
  const auto mapper = desc.mapper;
  const auto prgSize = desc.prgSize;

  if (mapper == 169 && prgSize >= 1024 * 1024) return "yuxing";
  if (mapper == 168 && prgSize >= 1024 * 1024) return "sb97";
  if (mapper == 171) {
    if (prgSize <= 128 * 1024) return "bbg1";
    if (prgSize <= 512 * 1024) return "sb2000";
    return "bbg98";
  }
  if (mapper == 179 && prgSize <= 512 * 1024) return "pec9588";
  return "fc";
}

}  // namespace

// ============================================================================
// C API 实现
// ============================================================================

extern "C" {

EmberEmulator* ember_create(void) {
  auto* emu = new (std::nothrow) EmberEmulator();
  if (!emu) return nullptr;

  try {
    // 打印机输出接收器（学习机模式用，FC 模式也不影响）
    emu->printerOutput = std::make_shared<WebPrinterOutput>();

    // FC 卡带模式：mapper 插件注册表
    ember::infra::mappers::PluginRegistryOptions options;
    options.bundledDirectory = "/plugins";
    options.userDirectory = "";
    options.hosts.hosts = {"fc"};
    options.circuits = std::make_shared<ember::bootstrap::StandardCircuitCatalog>();
    options.errors = nullptr;
    options.events = nullptr;
    emu->plugins = std::make_shared<ember::infra::mappers::PluginRegistry>(std::move(options));
    emu->loader = std::make_unique<ember::logic::cartridges::InesCartridgeLoader>(emu->plugins);

    // 学习机模式：主机型号目录
    // 注意：MachineCatalog 的构造函数接受 printerOutput 和 floppyActivity，
    // 我们传 printerOutput 进去，这样学习机的打印机会输出到 WebPrinterOutput
    emu->machineCatalog = std::make_unique<ember::bootstrap::MachineCatalog>(
        emu->printerOutput, nullptr);

    return emu;
  } catch (const std::exception& e) {
    emu->lastError = std::string("Failed to create emulator: ") + e.what();
    return emu;
  } catch (...) {
    emu->lastError = "Failed to create emulator: unknown error";
    return emu;
  }
}

void ember_destroy(EmberEmulator* emu) {
  if (!emu) return;
  if (emu->machine && emu->machine->isPoweredOn()) {
    try { emu->machine->powerOff(); } catch (...) {}
  }
  if (emu->cartridge && emu->machine && !emu->isLearningMachine) {
    try { emu->machine->cartridgeSlot().eject(); } catch (...) {}
  }
  emu->cartridge.reset();
  emu->machine.reset();
  emu->loader.reset();
  emu->plugins.reset();
  emu->machineCatalog.reset();
  emu->printerOutput.reset();
  delete emu;
}

void* ember_alloc(size_t size) {
  if (size == 0) return nullptr;
  return std::malloc(size);
}

void ember_free(void* ptr) {
  if (!ptr) return;
  std::free(ptr);
}

/*
 * 按机型名直接装载裸 ROM。
 *
 * 为什么需要这个接口：学习机固件（上游 ember/roms/*.bin）是**裸 PRG 数据**，
 * 没有 iNES 头，所以既没有 mapper 号也没有 PRG 大小，parseInes 会直接拒绝。
 * 实测 6 个机型的 .bin 开头字节各不相同，不存在可供识别的共同魔数，
 * 也就是说裸 ROM 无法自描述机型 —— 只能由调用方明确告知。
 *
 * 学习机分支本来就不需要 iNES 头（它要的只是 prg+chr 拼起来的整块 ROM），
 * 所以这里跳过 parseInes，直接把整份数据交给 MachineCatalog。
 * 机型名必须是 machine-catalog 认识的名字，非法名字由 createWithRom 抛错。
 * ROM 大小是否合法由各机型自己的构造函数校验（例如裕兴要求 1MB）。
 */
int ember_load_rom_as_machine(EmberEmulator* emu, const uint8_t* data, size_t size, const char* machineName) {
  if (!emu || !data || size == 0 || machineName == nullptr || machineName[0] == '\0') {
    if (emu) emu->lastError = "Invalid parameters";
    return -1;
  }

  return catchAll(emu, [&]() {
    if (emu->machine && emu->machine->isPoweredOn()) {
      emu->machine->powerOff();
    }
    if (emu->cartridge && emu->machine && !emu->isLearningMachine) {
      emu->machine->cartridgeSlot().eject();
      emu->cartridge.reset();
    }
    emu->machine.reset();
    emu->isLearningMachine = false;

    // 整份文件就是 ROM 本体，不做 iNES 解析
    std::vector<std::uint8_t> rom(data, data + size);
    emu->machine = emu->machineCatalog->createWithRom(machineName, std::move(rom));
    emu->isLearningMachine = true;
    emu->cartridge.reset();

    emu->frameCount = 0;
    emu->paused = false;
  });
}

int ember_load_rom(EmberEmulator* emu, const uint8_t* data, size_t size) {
  if (!emu || !data || size == 0) {
    if (emu) emu->lastError = "Invalid parameters";
    return -1;
  }

  return catchAll(emu, [&]() {
    if (emu->machine && emu->machine->isPoweredOn()) {
      emu->machine->powerOff();
    }
    if (emu->cartridge && emu->machine && !emu->isLearningMachine) {
      emu->machine->cartridgeSlot().eject();
      emu->cartridge.reset();
    }
    emu->machine.reset();
    emu->isLearningMachine = false;

    std::span<const uint8_t> fileSpan(data, size);
    auto image = ember::logic::cartridges::parseInes(fileSpan);
    const std::string machineType = detectMachineType(image.description);

    if (machineType == "fc") {
      // ---- FC 卡带模式 ----
      emu->machine = std::make_unique<ember::bootstrap::FcMachine>();
      emu->isLearningMachine = false;

      auto plan = emu->loader->plan(fileSpan);
      emu->cartridge = emu->loader->assemble(
          plan,
          emu->machine->cartridgeIrq(),
          *emu->machine
      );
      emu->machine->cartridgeSlot().insert(*emu->cartridge);
    } else {
      // ---- 学习机模式 ----
      std::vector<std::uint8_t> rom;
      rom.reserve(image.prg.size() + image.chr.size());
      rom.insert(rom.end(), image.prg.begin(), image.prg.end());
      rom.insert(rom.end(), image.chr.begin(), image.chr.end());

      emu->machine = emu->machineCatalog->createWithRom(machineType, std::move(rom));
      emu->isLearningMachine = true;
      emu->cartridge.reset();
    }

    emu->frameCount = 0;
    emu->paused = false;
  });
}

void ember_power_on(EmberEmulator* emu) {
  if (!emu || !emu->machine) return;
  catchAll(emu, [&]() {
    if (!emu->machine->isPoweredOn()) emu->machine->powerOn();
    emu->paused = false;
  });
}

void ember_power_off(EmberEmulator* emu) {
  if (!emu || !emu->machine) return;
  catchAll(emu, [&]() {
    if (emu->machine->isPoweredOn()) emu->machine->powerOff();
    emu->paused = false;
  });
}

void ember_reset(EmberEmulator* emu) {
  if (!emu || !emu->machine) return;
  catchAll(emu, [&]() {
    if (emu->machine->isPoweredOn()) emu->machine->softReset();
  });
}

void ember_pause(EmberEmulator* emu) { if (emu) emu->paused = true; }
void ember_resume(EmberEmulator* emu) { if (emu) emu->paused = false; }

int ember_is_running(EmberEmulator* emu) {
  if (!emu || !emu->machine) return 0;
  return emu->machine->isPoweredOn() && !emu->paused ? 1 : 0;
}

int ember_is_powered_on(EmberEmulator* emu) {
  if (!emu || !emu->machine) return 0;
  return emu->machine->isPoweredOn() ? 1 : 0;
}

uint64_t ember_run_frame(EmberEmulator* emu) {
  if (!emu || !emu->machine || !emu->machine->isPoweredOn() || emu->paused) return 0;
  uint64_t result = 0;
  catchAll(emu, [&]() {
    emu->machine->runFrame();
    emu->frameCount++;
    result = emu->frameCount;
    if (emu->cartridge && !emu->isLearningMachine) {
      auto failure = emu->cartridge->failure();
      if (failure.has_value()) {
        emu->lastError = "Mapper script error: " + failure->message +
                         " (" + failure->code + ")";
        emu->paused = true;
      }
    }
  });
  return result;
}

const uint8_t* ember_get_framebuffer(EmberEmulator* emu) {
  if (!emu || !emu->machine) return nullptr;
  auto fb = emu->machine->frame();
  return reinterpret_cast<const uint8_t*>(fb.pixels.data());
}

size_t ember_copy_framebuffer(EmberEmulator* emu, uint8_t* out, size_t max_bytes) {
  if (!emu || !emu->machine || !out) return 0;
  auto fb = emu->machine->frame();
  const size_t needed = fb.pixels.size_bytes();
  if (needed == 0 || needed > max_bytes) return 0;
  std::memcpy(out, fb.pixels.data(), needed);
  return needed;
}

uint32_t ember_get_frame_width(EmberEmulator* emu) {
  if (!emu || !emu->machine) return 0;
  return emu->machine->frame().width;
}

uint32_t ember_get_frame_height(EmberEmulator* emu) {
  if (!emu || !emu->machine) return 0;
  return emu->machine->frame().height;
}

void ember_set_controller1(EmberEmulator* emu, uint8_t buttons) {
  if (!emu || !emu->machine) return;
  emu->machine->setButtons(0, buttons);
}

void ember_set_controller2(EmberEmulator* emu, uint8_t buttons) {
  if (!emu || !emu->machine) return;
  emu->machine->setButtons(1, buttons);
}

int ember_get_audio_samples(EmberEmulator* emu, float* out_samples, int max_samples) {
  if (!emu || !emu->machine || !out_samples || max_samples <= 0) return 0;
  int count = 0;
  catchAll(emu, [&]() {
    emu->audioBuffer.clear();
    emu->machine->takeAudio(emu->audioBuffer);
    count = static_cast<int>(std::min(emu->audioBuffer.size(), static_cast<size_t>(max_samples)));
    if (count > 0) {
      std::memcpy(out_samples, emu->audioBuffer.data(), count * sizeof(float));
    }
  });
  return count;
}

int ember_get_audio_sample_rate(EmberEmulator* emu) {
  if (!emu || !emu->machine) return 0;
  return static_cast<int>(emu->machine->audioSampleRate());
}

double ember_get_fps(EmberEmulator* emu) {
  if (!emu || !emu->machine) return 0.0;
  return emu->machine->framesPerSecond();
}

const char* ember_get_error(EmberEmulator* emu) {
  if (!emu) return nullptr;
  return emu->lastError.empty() ? nullptr : emu->lastError.c_str();
}

// ============================================================================
// 键盘
// ============================================================================

int ember_send_key(EmberEmulator* emu, const char* code, int down) {
  if (!emu || !emu->machine || !code) return -1;
  return catchAll(emu, [&]() {
    ember::contracts::hardware::KeyInput key;
    key.code = code;
    key.down = down != 0;
    emu->machine->input(ember::contracts::hardware::InputEvent{key});
  });
}

int ember_has_keyboard(EmberEmulator* emu) {
  if (!emu || !emu->machine) return 0;
  return emu->machine->keyboardIsMachineInput() ? 1 : 0;
}

// ============================================================================
// 鼠标
// ============================================================================

int ember_send_mouse(EmberEmulator* emu, int kind, double x, double y,
                     double dx, double dy, uint8_t buttons, double wheel) {
  if (!emu || !emu->machine) return -1;
  return catchAll(emu, [&]() {
    ember::contracts::hardware::MouseInput mouse;
    using MK = ember::contracts::hardware::MouseKind;
    switch (kind) {
      case EMBER_MOUSE_DOWN:  mouse.kind = MK::Down; break;
      case EMBER_MOUSE_UP:    mouse.kind = MK::Up; break;
      case EMBER_MOUSE_WHEEL: mouse.kind = MK::Wheel; break;
      case EMBER_MOUSE_LEAVE: mouse.kind = MK::Leave; break;
      default:                mouse.kind = MK::Move; break;
    }
    mouse.x = x;
    mouse.y = y;
    mouse.dx = dx;
    mouse.dy = dy;
    mouse.buttons = buttons;
    mouse.wheel = wheel;
    emu->machine->input(ember::contracts::hardware::InputEvent{mouse});
  });
}

int ember_has_mouse(EmberEmulator* emu) {
  if (!emu || !emu->machine) return 0;
  return emu->machine->hasMouse() ? 1 : 0;
}

// ============================================================================
// 软驱
// ============================================================================

unsigned ember_floppy_drive_count(EmberEmulator* emu) {
  if (!emu || !emu->machine) return 0;
  return emu->machine->floppyDriveCount();
}

int ember_floppy_insert(EmberEmulator* emu, unsigned index,
                        const uint8_t* data, size_t size,
                        int write_protected, const char* name) {
  if (!emu || !emu->machine || !data || size == 0) return -1;
  auto* drive = emu->machine->floppyDrive(index);
  if (!drive) return -2;
  return catchAll(emu, [&]() {
    std::vector<std::uint8_t> image(data, data + size);
    drive->insertImage(std::move(image), write_protected != 0,
                       name ? std::string(name) : std::string());
  });
}

int ember_floppy_eject(EmberEmulator* emu, unsigned index) {
  if (!emu || !emu->machine) return -1;
  auto* drive = emu->machine->floppyDrive(index);
  if (!drive) return -2;
  drive->ejectImage();
  return 0;
}

int ember_floppy_inserted(EmberEmulator* emu, unsigned index) {
  if (!emu || !emu->machine) return 0;
  auto* drive = emu->machine->floppyDrive(index);
  return drive && drive->inserted() ? 1 : 0;
}

int ember_floppy_motor_running(EmberEmulator* emu, unsigned index) {
  if (!emu || !emu->machine) return 0;
  auto* drive = emu->machine->floppyDrive(index);
  if (!drive) return 0;
  return drive->motorRunning() ? 1 : 0;
}

int ember_floppy_modified(EmberEmulator* emu, unsigned index) {
  if (!emu || !emu->machine) return 0;
  auto* drive = emu->machine->floppyDrive(index);
  return drive && drive->modified() ? 1 : 0;
}

const uint8_t* ember_floppy_get_image(EmberEmulator* emu, unsigned index, size_t* out_size) {
  if (!emu || !emu->machine || !out_size) return nullptr;
  auto* drive = emu->machine->floppyDrive(index);
  if (!drive || !drive->inserted()) {
    *out_size = 0;
    return nullptr;
  }
  const auto& img = drive->image();
  *out_size = img.size();
  return img.data();
}

// ============================================================================
// 打印机
// ============================================================================

int ember_has_printer(EmberEmulator* emu) {
  if (!emu || !emu->machine) return 0;
  return emu->machine->printer() != nullptr ? 1 : 0;
}

uint64_t ember_printer_pages_output(EmberEmulator* emu) {
  if (!emu || !emu->printerOutput) return 0;
  return emu->printerOutput->pagesOutput();
}

const uint8_t* ember_printer_get_last_page(EmberEmulator* emu,
                                           unsigned* out_width,
                                           unsigned* out_height,
                                           size_t* out_size) {
  if (!emu || !emu->printerOutput || !out_width || !out_height || !out_size) return nullptr;
  auto* page = emu->printerOutput->lastPage();
  if (!page) {
    *out_width = 0;
    *out_height = 0;
    *out_size = 0;
    return nullptr;
  }
  *out_width = page->width;
  *out_height = page->height;
  *out_size = page->rows.size();
  return page->rows.data();
}

// ============================================================================
// 磁带机
// ============================================================================

int ember_has_tape(EmberEmulator* emu) {
  if (!emu || !emu->machine) return 0;
  return emu->machine->tape() != nullptr ? 1 : 0;
}

int ember_tape_load(EmberEmulator* emu, const uint8_t* data, size_t size) {
  if (!emu || !emu->machine || !data || size == 0) return -1;
  auto* tape = emu->machine->tape();
  if (!tape) return -2;
  return catchAll(emu, [&]() {
    // 数据按 96kHz 16位 PCM 处理；如果是奇数个字节就截掉最后一个
    const size_t sampleBytes = size - (size % 2);
    const size_t frameCount = sampleBytes / 2;  // 先假设单声道
    std::vector<std::int16_t> samples(frameCount);
    std::memcpy(samples.data(), data, sampleBytes);
    auto source = std::make_unique<VectorTapeSource>(std::move(samples), false);
    tape->load(std::move(source), "web-tape");
  });
}

void ember_tape_rewind(EmberEmulator* emu) {
  if (!emu || !emu->machine) return;
  auto* tape = emu->machine->tape();
  if (tape && tape->loaded()) {
    tape->seek(0.0);
  }
}

int ember_tape_is_playing(EmberEmulator* emu) {
  if (!emu || !emu->machine) return 0;
  auto* tape = emu->machine->tape();
  if (!tape) return 0;
  using TM = ember::contracts::hardware::TapeMode;
  return tape->mode() == TM::Playing ? 1 : 0;
}

}  // extern "C"
