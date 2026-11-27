/*
 * ------------------------------------------------------------------------
 * 名称：ember_web_api.hpp
 * 说明：Ember FC 模拟器 WebAssembly C API：简化的接口，直接基于 Machine 层，
 *       避开 Core/RequestRouter/ThreadPool 等多线程架构，更适合 Web 环境。
 *       支持 FC 卡带模式和学习机模式（裕兴/小霸王/步步高/金字塔等）。
 * 备注：所有函数均为 C 接口，供 Embind/JavaScript 调用。
 *       跨 wasm 边界的缓冲区一律传「wasm 内存地址」，
 *       JavaScript 侧通过 Module.HEAPU8 / Module.HEAPF32 读写，不要直接传 JS 数组。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <cstdint>
#include <cstddef>

#ifdef __cplusplus
extern "C" {
#endif

/** 手柄按键掩码。 */
#define EMBER_PAD_A      0x01
#define EMBER_PAD_B      0x02
#define EMBER_PAD_SELECT 0x04
#define EMBER_PAD_START  0x08
#define EMBER_PAD_UP     0x10
#define EMBER_PAD_DOWN   0x20
#define EMBER_PAD_LEFT   0x40
#define EMBER_PAD_RIGHT  0x80

/** 鼠标按键位。 */
#define EMBER_MOUSE_LEFT   1
#define EMBER_MOUSE_RIGHT  2
#define EMBER_MOUSE_MIDDLE 4

/** 鼠标事件类型。 */
#define EMBER_MOUSE_MOVE   0
#define EMBER_MOUSE_DOWN   1
#define EMBER_MOUSE_UP     2
#define EMBER_MOUSE_WHEEL  3
#define EMBER_MOUSE_LEAVE  4

/** 模拟器实例句柄。 */
typedef struct EmberEmulator EmberEmulator;

/** 创建模拟器实例。 */
EmberEmulator* ember_create(void);

/** 销毁模拟器实例。 */
void ember_destroy(EmberEmulator* emu);

/**
 * 在 wasm 内存里分配一块缓冲区。
 * 供 JavaScript 侧暂存 ROM / 软盘镜像等数据。
 * @param size 字节数。
 * @return 缓冲区起始地址（wasm 内存地址）；分配失败返回 NULL。
 */
void* ember_alloc(size_t size);

/**
 * 释放 ember_alloc 分配的缓冲区。
 * @param ptr 缓冲区地址；传 NULL 不做处理。
 */
void ember_free(void* ptr);

/**
 * 加载 iNES 格式 ROM（自动识别 FC 卡带或学习机）。
 * @param data ROM 数据指针（wasm 内存地址）。
 * @param size 数据大小（字节）。
 * @return 0 成功，非 0 失败。
 */
int ember_load_rom(EmberEmulator* emu, const uint8_t* data, size_t size);

/**
 * 按机型名直接装载裸 ROM（无 iNES 头的学习机固件，例如上游 ember/roms/*.bin）。
 *
 * 跳过 iNES 解析，整份数据当作 ROM 本体交给指定机型。ROM 大小是否合法
 * 由机型自己校验，名字非法或大小不对都会通过 lastError 报出来。
 *
 * @param emu 模拟器实例。
 * @param data ROM 数据（裸 PRG，不含文件头）。
 * @param size 数据大小（字节）。
 * @param machineName 机型名，必须是 machine-catalog 认识的名字（yuxing/bbg98/...）。
 * @return 0 成功，非 0 失败。
 */
int ember_load_rom_as_machine(EmberEmulator* emu, const uint8_t* data, size_t size, const char* machineName);

/** 上电并运行。 */
void ember_power_on(EmberEmulator* emu);

/** 断电。 */
void ember_power_off(EmberEmulator* emu);

/** 软复位。 */
void ember_reset(EmberEmulator* emu);

/** 暂停。 */
void ember_pause(EmberEmulator* emu);

/** 继续运行。 */
void ember_resume(EmberEmulator* emu);

/** 是否正在运行。 */
int ember_is_running(EmberEmulator* emu);

/** 是否通电。 */
int ember_is_powered_on(EmberEmulator* emu);

/**
 * 运行一帧画面。
 * @return 帧号。
 */
uint64_t ember_run_frame(EmberEmulator* emu);

/**
 * 获取帧缓冲（RGBA8，256x240）。
 * @return 指向像素数据的指针（wasm 内存地址）。
 */
const uint8_t* ember_get_framebuffer(EmberEmulator* emu);

/**
 * 把当前帧缓冲拷贝到调用方的缓冲区。
 * @param out 输出缓冲区（wasm 内存地址）。
 * @param max_bytes 输出缓冲区容量（字节）。
 * @return 实际拷贝的字节数；容量不足时返回 0。
 */
size_t ember_copy_framebuffer(EmberEmulator* emu, uint8_t* out, size_t max_bytes);

/** 帧宽度。 */
uint32_t ember_get_frame_width(EmberEmulator* emu);

/** 帧高度。 */
uint32_t ember_get_frame_height(EmberEmulator* emu);

/**
 * 设置手柄 1 的按键。
 * @param buttons 按键掩码。
 */
void ember_set_controller1(EmberEmulator* emu, uint8_t buttons);

/**
 * 设置手柄 2 的按键。
 * @param buttons 按键掩码。
 */
void ember_set_controller2(EmberEmulator* emu, uint8_t buttons);

/**
 * 获取音频采样（单声道，float -1.0 ~ 1.0）。
 * @param out_samples 输出缓冲区（wasm 内存地址）。
 * @param max_samples 缓冲区最大采样数。
 * @return 实际写入的采样数。
 */
int ember_get_audio_samples(EmberEmulator* emu, float* out_samples, int max_samples);

/** 音频采样率。 */
int ember_get_audio_sample_rate(EmberEmulator* emu);

/** 每秒帧数。 */
double ember_get_fps(EmberEmulator* emu);

/** 获取最后一条错误消息。 */
const char* ember_get_error(EmberEmulator* emu);

// ============================================================================
// 键盘输入（学习机必备）
// ============================================================================

/**
 * 发送键盘事件。
 * @param code 物理键位，取 KeyboardEvent.code，如 "KeyA"、"Enter"、"Space"。
 * @param down 1 按下，0 松开。
 * @return 0 成功，非 0 失败。
 */
int ember_send_key(EmberEmulator* emu, const char* code, int down);

/**
 * 当前机型是否有内置键盘（学习机为 true，FC 为 false）。
 * 用于前端决定是否显示虚拟键盘或捕获键盘输入。
 */
int ember_has_keyboard(EmberEmulator* emu);

// ============================================================================
// 鼠标输入
// ============================================================================

/**
 * 发送鼠标事件。
 * @param kind 事件类型（EMBER_MOUSE_MOVE/DOWN/UP/WHEEL/LEAVE）。
 * @param x 画面 X 坐标（256x240 标准帧像素）。
 * @param y 画面 Y 坐标。
 * @param dx 相对位移 X。
 * @param dy 相对位移 Y。
 * @param buttons 当前按住的按键位（EMBER_MOUSE_LEFT/RIGHT/MIDDLE）。
 * @param wheel 滚轮滚动量（向下为正）。
 * @return 0 成功，非 0 失败。
 */
int ember_send_mouse(EmberEmulator* emu, int kind, double x, double y,
                     double dx, double dy, uint8_t buttons, double wheel);

/** 当前机型是否支持鼠标。 */
int ember_has_mouse(EmberEmulator* emu);

// ============================================================================
// 软驱（磁盘）
// ============================================================================

/** 软驱数量。 */
unsigned ember_floppy_drive_count(EmberEmulator* emu);

/**
 * 插入软盘镜像到指定软驱。
 * @param index 软驱编号（从 0 开始）。
 * @param data 镜像数据指针（wasm 内存地址）。
 * @param size 镜像大小（字节）。
 * @param write_protected 是否写保护（1 是，0 否）。
 * @param name 镜像名称/来源标识（字符串）。
 * @return 0 成功，非 0 失败。
 */
int ember_floppy_insert(EmberEmulator* emu, unsigned index,
                        const uint8_t* data, size_t size,
                        int write_protected, const char* name);

/**
 * 弹出指定软驱的盘片。
 * @param index 软驱编号。
 * @return 0 成功，非 0 失败。
 */
int ember_floppy_eject(EmberEmulator* emu, unsigned index);

/**
 * 指定软驱里是否有盘片。
 * @param index 软驱编号。
 * @return 1 有盘片，0 没有或编号无效。
 */
int ember_floppy_inserted(EmberEmulator* emu, unsigned index);

/**
 * 盘片是否已修改（需要保存）。
 * @param index 软驱编号。
 * @return 1 已修改，0 未修改或编号无效。
 */
/** 软驱马达是否在转：真机的读写灯跟马达同亮同灭，界面据此点亮指示灯。 */
int ember_floppy_motor_running(EmberEmulator* emu, unsigned index);

int ember_floppy_modified(EmberEmulator* emu, unsigned index);

/**
 * 获取盘片镜像数据（用于保存到本地）。
 * @param index 软驱编号。
 * @param out_size 输出：镜像大小（字节）。
 * @return 镜像数据指针（wasm 内存地址）；没有盘片返回 NULL。
 * @remarks 指针指向内部数据，调用 eject 或 insert 后失效。
 */
const uint8_t* ember_floppy_get_image(EmberEmulator* emu, unsigned index, size_t* out_size);

// ============================================================================
// 打印机
// ============================================================================

/** 当前机型是否有打印机。 */
int ember_has_printer(EmberEmulator* emu);

/**
 * 自启动以来输出的页数。
 * @return 页数。
 */
uint64_t ember_printer_pages_output(EmberEmulator* emu);

/**
 * 获取最新一页打印输出（位图）。
 * @param out_width 输出：页宽（像素）。
 * @param out_height 输出：页高（像素）。
 * @param out_size 输出：位图数据大小（字节）。
 * @return 位图数据指针（1 位每像素，每行按字节补齐，高位在左）；没有页返回 NULL。
 */
const uint8_t* ember_printer_get_last_page(EmberEmulator* emu,
                                           unsigned* out_width,
                                           unsigned* out_height,
                                           size_t* out_size);

// ============================================================================
// 磁带机
// ============================================================================

/** 当前机型是否有磁带机接口。 */
int ember_has_tape(EmberEmulator* emu);

/**
 * 载入磁带音频数据。
 * @param data 音频数据（96kHz 16位 PCM，左右交错）。
 * @param size 数据大小（字节）。
 * @return 0 成功，非 0 失败。
 */
int ember_tape_load(EmberEmulator* emu, const uint8_t* data, size_t size);

/** 倒带。 */
void ember_tape_rewind(EmberEmulator* emu);

/**
 * 磁带是否正在播放。
 * @return 1 播放中，0 未播放。
 */
int ember_tape_is_playing(EmberEmulator* emu);

#ifdef __cplusplus
}
#endif
