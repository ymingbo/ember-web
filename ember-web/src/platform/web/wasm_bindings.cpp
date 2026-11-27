/*
 * ------------------------------------------------------------------------
 * 名称：wasm_bindings.cpp
 * 说明：Embind 绑定：把 C API 暴露给 JavaScript。
 *       跨边界的缓冲区都用「wasm 内存地址」传递，
 *       JavaScript 侧用 Module.HEAPU8 / Module.HEAPF32 读写对应内存。
 * ------------------------------------------------------------------------
 */

#include <vector>
#include <emscripten.h>
#include <emscripten/bind.h>
#include <emscripten/val.h>

#include <cstdint>
#include <string>

#include "platform/web/ember_web_api.hpp"

using namespace emscripten;

namespace {

using WasmAddress = uint32_t;

}  // namespace

class EmberWasm {
 public:
  EmberWasm() : emu_(ember_create()) {}
  ~EmberWasm() {
    if (emu_) { ember_destroy(emu_); emu_ = nullptr; }
  }

  EmberWasm(const EmberWasm&) = delete;
  EmberWasm& operator=(const EmberWasm&) = delete;

  // ---- 内存管理 ----
  WasmAddress allocBuffer(size_t size) {
    if (size == 0) return 0;
    void* p = ember_alloc(size);
    return static_cast<WasmAddress>(reinterpret_cast<uintptr_t>(p));
  }
  void freeBuffer(WasmAddress address) {
    if (address == 0) return;
    ember_free(reinterpret_cast<void*>(static_cast<uintptr_t>(address)));
  }

  // ---- ROM 加载 ----
  int loadRom(WasmAddress address, size_t size) {
    if (!emu_ || address == 0 || size == 0) return -1;
    const auto* data = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(address));
    return ember_load_rom(emu_, data, size);
  }
  /* 按机型名装载裸 ROM（学习机 .bin 固件用）。machineName 为空时行为同 loadRom。 */
  int loadRomAsMachine(WasmAddress address, size_t size, const std::string& machineName) {
    if (!emu_ || address == 0 || size == 0) return -1;
    const auto* data = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(address));
    return ember_load_rom_as_machine(emu_, data, size, machineName.c_str());
  }

  // ---- 电源控制 ----
  void powerOn() { if (emu_) ember_power_on(emu_); }
  void powerOff() { if (emu_) ember_power_off(emu_); }
  void reset() { if (emu_) ember_reset(emu_); }
  void pause() { if (emu_) ember_pause(emu_); }
  void resume() { if (emu_) ember_resume(emu_); }
  bool isRunning() const { return emu_ && ember_is_running(emu_) != 0; }
  bool isPoweredOn() const { return emu_ && ember_is_powered_on(emu_) != 0; }

  // ---- 帧 ----
  /* 返回 double 而不是 uint64_t：Embind 里 uint64_t 属于「unbound type」，
     调用时直接抛 `Cannot call EmberWasm.runFrame due to unbound types`，
     表现就是主循环第一帧就崩、FPS 永远 0.0、画面全黑。
     double 在 Embind 里有原生绑定，且这里只是帧计数，双精度完全装得下。 */
  double runFrame() { return emu_ ? static_cast<double>(ember_run_frame(emu_)) : 0.0; }
  WasmAddress getFramebufferPtr() const {
    if (!emu_) return 0;
    return static_cast<WasmAddress>(reinterpret_cast<uintptr_t>(ember_get_framebuffer(emu_)));
  }
  int copyFramebuffer(WasmAddress dst, size_t max_bytes) {
    if (!emu_ || dst == 0) return 0;
    auto* out = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(dst));
    return static_cast<int>(ember_copy_framebuffer(emu_, out, max_bytes));
  }
  uint32_t getFrameWidth() const { return emu_ ? ember_get_frame_width(emu_) : 0; }
  uint32_t getFrameHeight() const { return emu_ ? ember_get_frame_height(emu_) : 0; }

  // ---- 手柄 ----
  void setController1(uint8_t buttons) { if (emu_) ember_set_controller1(emu_, buttons); }
  void setController2(uint8_t buttons) { if (emu_) ember_set_controller2(emu_, buttons); }

  // ---- 音频 ----
  int getAudioSamples(WasmAddress dst, int max_samples) {
    if (!emu_ || dst == 0 || max_samples <= 0) return 0;
    auto* out = reinterpret_cast<float*>(static_cast<uintptr_t>(dst));
    return ember_get_audio_samples(emu_, out, max_samples);
  }
  int getAudioSampleRate() const { return emu_ ? ember_get_audio_sample_rate(emu_) : 0; }
  double getFps() const { return emu_ ? ember_get_fps(emu_) : 0.0; }

  // ---- 错误 ----
  std::string getError() const {
    if (!emu_) return "No emulator instance";
    const char* err = ember_get_error(emu_);
    return err ? std::string(err) : std::string();
  }

  // ====================================================================
  // 键盘
  // ====================================================================
  int sendKey(const std::string& code, bool down) {
    if (!emu_) return -1;
    return ember_send_key(emu_, code.c_str(), down ? 1 : 0);
  }
  bool hasKeyboard() const { return emu_ && ember_has_keyboard(emu_) != 0; }

  // ====================================================================
  // 鼠标
  // ====================================================================
  int sendMouse(int kind, double x, double y, double dx, double dy,
                uint8_t buttons, double wheel) {
    if (!emu_) return -1;
    return ember_send_mouse(emu_, kind, x, y, dx, dy, buttons, wheel);
  }
  bool hasMouse() const { return emu_ && ember_has_mouse(emu_) != 0; }
  // ====================================================================
  // 软驱
  // ====================================================================
  unsigned floppyDriveCount() const {
    return emu_ ? ember_floppy_drive_count(emu_) : 0;
  }
  int floppyInsert(unsigned index, WasmAddress dataAddr, size_t size,
                   bool writeProtected, const std::string& name) {
    if (!emu_ || dataAddr == 0 || size == 0) return -1;
    const auto* data = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(dataAddr));
    return ember_floppy_insert(emu_, index, data, size,
                               writeProtected ? 1 : 0, name.c_str());
  }
  int floppyEject(unsigned index) {
    return emu_ ? ember_floppy_eject(emu_, index) : -1;
  }
  bool floppyInserted(unsigned index) const {
    return emu_ && ember_floppy_inserted(emu_, index) != 0;
  }
  bool floppyModified(unsigned index) const {
    return emu_ && ember_floppy_modified(emu_, index) != 0;
  }
  /** 软驱马达是否在转；真机的读写灯跟马达同亮同灭。 */
  bool floppyMotorRunning(unsigned index) const {
    return emu_ && ember_floppy_motor_running(emu_, index) != 0;
  }
  /** 软盘镜像指针。尺寸写在 lastImageSize 里（Embind 不支持引用出参）。 */
  WasmAddress floppyGetImage(unsigned index) {
    if (!emu_) { lastImageSize = 0; return 0; }
    size_t size = 0;
    const uint8_t* data = ember_floppy_get_image(emu_, index, &size);
    lastImageSize = size;
    return data ? static_cast<WasmAddress>(reinterpret_cast<uintptr_t>(data)) : 0;
  }

  // ====================================================================
  // 打印机
  // ====================================================================
  bool hasPrinter() const { return emu_ && ember_has_printer(emu_) != 0; }
  /* 同样避开 uint64_t：Embind 不认，调用即抛 unbound types。 */
  double printerPagesOutput() const {
    return emu_ ? static_cast<double>(ember_printer_pages_output(emu_)) : 0.0;
  }
  /** 打印机页面指针。宽高和尺寸写在 lastPageW/H/Size 里。 */
  WasmAddress printerGetLastPage() {
    if (!emu_) { lastPageW = 0; lastPageH = 0; lastPageSize = 0; return 0; }
    unsigned w = 0, h = 0;
    size_t s = 0;
    const uint8_t* data = ember_printer_get_last_page(emu_, &w, &h, &s);
    lastPageW = w;
    lastPageH = h;
    lastPageSize = s;
    return data ? static_cast<WasmAddress>(reinterpret_cast<uintptr_t>(data)) : 0;
  }

  // ====================================================================
  // 磁带机
  // ====================================================================
  bool hasTape() const { return emu_ && ember_has_tape(emu_) != 0; }
  int tapeLoad(WasmAddress dataAddr, size_t size) {
    if (!emu_ || dataAddr == 0 || size == 0) return -1;
    const auto* data = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(dataAddr));
    return ember_tape_load(emu_, data, size);
  }
  void tapeRewind() { if (emu_) ember_tape_rewind(emu_); }
  bool tapeIsPlaying() const { return emu_ && ember_tape_is_playing(emu_) != 0; }

  EmberEmulator* handle() const { return emu_; }

 public:
  /** 上一次 floppyGetImage 的镜像字节数（Embind 无引用出参，尺寸走这里）。 */
  size_t lastImageSize{0};
  /** 上一次 printerGetLastPage 的页面宽高与字节数。 */
  unsigned lastPageW{0};
  unsigned lastPageH{0};
  size_t lastPageSize{0};

 private:
  EmberEmulator* emu_{nullptr};
};

EMSCRIPTEN_BINDINGS(ember_wasm) {
  class_<EmberWasm>("EmberWasm")
      .constructor<>()
      // 内存管理
      .function("allocBuffer", &EmberWasm::allocBuffer)
      .function("freeBuffer", &EmberWasm::freeBuffer)
      // ROM 加载
      .function("loadRom", &EmberWasm::loadRom)
      .function("loadRomAsMachine", &EmberWasm::loadRomAsMachine)
      // 电源控制
      .function("powerOn", &EmberWasm::powerOn)
      .function("powerOff", &EmberWasm::powerOff)
      .function("reset", &EmberWasm::reset)
      .function("pause", &EmberWasm::pause)
      .function("resume", &EmberWasm::resume)
      .function("isRunning", &EmberWasm::isRunning)
      .function("isPoweredOn", &EmberWasm::isPoweredOn)
      // 帧
      .function("runFrame", &EmberWasm::runFrame)
      .function("getFramebufferPtr", &EmberWasm::getFramebufferPtr)
      .function("copyFramebuffer", &EmberWasm::copyFramebuffer)
      .function("getFrameWidth", &EmberWasm::getFrameWidth)
      .function("getFrameHeight", &EmberWasm::getFrameHeight)
      // 手柄
      .function("setController1", &EmberWasm::setController1)
      .function("setController2", &EmberWasm::setController2)
      // 音频
      .function("getAudioSamples", &EmberWasm::getAudioSamples)
      .function("getAudioSampleRate", &EmberWasm::getAudioSampleRate)
      .function("getFps", &EmberWasm::getFps)
      // 错误
      .function("getError", &EmberWasm::getError)
      // 键盘
      .function("sendKey", &EmberWasm::sendKey)
      .function("hasKeyboard", &EmberWasm::hasKeyboard)
      // 鼠标
      .function("sendMouse", &EmberWasm::sendMouse)
      .function("hasMouse", &EmberWasm::hasMouse)
      // 软驱
      .function("floppyDriveCount", &EmberWasm::floppyDriveCount)
      .function("floppyInsert", &EmberWasm::floppyInsert)
      .function("floppyEject", &EmberWasm::floppyEject)
      .function("floppyInserted", &EmberWasm::floppyInserted)
      .function("floppyModified", &EmberWasm::floppyModified)
      .function("floppyMotorRunning", &EmberWasm::floppyMotorRunning)
      .function("floppyGetImage", &EmberWasm::floppyGetImage)
      // 打印机
      .function("hasPrinter", &EmberWasm::hasPrinter)
      .function("printerPagesOutput", &EmberWasm::printerPagesOutput)
      .function("printerGetLastPage", &EmberWasm::printerGetLastPage)
      .property("lastImageSize", &EmberWasm::lastImageSize)
      .property("lastPageW", &EmberWasm::lastPageW)
      .property("lastPageH", &EmberWasm::lastPageH)
      .property("lastPageSize", &EmberWasm::lastPageSize)
      // 磁带机
      .function("hasTape", &EmberWasm::hasTape)
      .function("tapeLoad", &EmberWasm::tapeLoad)
      .function("tapeRewind", &EmberWasm::tapeRewind)
      .function("tapeIsPlaying", &EmberWasm::tapeIsPlaying);

  // 手柄按键常量
  constant("PAD_A", EMBER_PAD_A);
  constant("PAD_B", EMBER_PAD_B);
  constant("PAD_SELECT", EMBER_PAD_SELECT);
  constant("PAD_START", EMBER_PAD_START);
  constant("PAD_UP", EMBER_PAD_UP);
  constant("PAD_DOWN", EMBER_PAD_DOWN);
  constant("PAD_LEFT", EMBER_PAD_LEFT);
  constant("PAD_RIGHT", EMBER_PAD_RIGHT);

  // 鼠标事件类型常量
  constant("MOUSE_MOVE", EMBER_MOUSE_MOVE);
  constant("MOUSE_DOWN", EMBER_MOUSE_DOWN);
  constant("MOUSE_UP", EMBER_MOUSE_UP);
  constant("MOUSE_WHEEL", EMBER_MOUSE_WHEEL);
  constant("MOUSE_LEAVE", EMBER_MOUSE_LEAVE);

  // 鼠标按键常量
  constant("MOUSE_LEFT", EMBER_MOUSE_LEFT);
  constant("MOUSE_RIGHT", EMBER_MOUSE_RIGHT);
  constant("MOUSE_MIDDLE", EMBER_MOUSE_MIDDLE);
}
