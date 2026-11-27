/*
 * ------------------------------------------------------------------------
 * 名称：manifest.hpp
 * 说明：mapper 插件清单（manifest.json）的数据结构、解析与检查。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-14
 * 备注：字段与规则见 docs/03-mapper插件规范.md 第 4 节；不认识的字段、缺字段、越界、窗口重叠等都拒绝，错误码为 mappers.invalidPackage，说明指出出错的字段。
 *       寄存器的读写可以是“回调脚本”（true），也可以是由宿主直接执行的声明（固定值、状态、窗口的 bank、查表）；磁带的比较器与边沿计时也可以声明。
 *       设置项本阶段只解析与检查，运行时的行为属于后续阶段。
 * ------------------------------------------------------------------------
 */

#pragma once

#include <compare>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "contracts/mappers/cartridge-image.hpp"

namespace ember::infra::mappers {

/** 插件接口版本，当前为 1。 */
inline constexpr std::uint32_t SUPPORTED_API_VERSION = 1;

/** 一张查表的索引最多由几个来源拼成。 */
inline constexpr std::size_t MAX_INDEX_SOURCES = 8;

/** 版本号：主.次.修订。 */
struct Version {
  std::uint32_t major{0};
  std::uint32_t minor{0};
  std::uint32_t patch{0};

  friend auto operator<=>(const Version&, const Version&) = default;
};

/** 总线上的地址范围，两端都包含。 */
struct AddressRange {
  std::uint32_t first{0};
  std::uint32_t last{0};

  [[nodiscard]] std::uint32_t length() const noexcept { return last - first + 1; }
  [[nodiscard]] bool overlaps(const AddressRange& other) const noexcept { return first <= other.last && other.first <= last; }
};

/** 一项匹配条件。 */
struct MatchRule {
  std::uint32_t mapper{0};
  std::optional<std::uint32_t> variant;
  std::optional<std::uint64_t> prgSize;
  std::optional<std::uint64_t> chrSize;
  std::optional<std::uint32_t> crc32Prg;
  std::optional<std::uint32_t> crc32All;
  /** 有无 CHR ROM：true 表示文件里有 CHR，false 表示没有。 */
  std::optional<bool> chrPresent;

  /** 写出的条件数，mapper 号也算一项。 */
  [[nodiscard]] int conditionCount() const noexcept;

  /** 描述是否满足全部写出的条件。 */
  [[nodiscard]] bool matches(const contracts::mappers::CartridgeDescription& description) const noexcept;
};

/** rom 的数据来源。 */
enum class StorageSource { None, FilePrg, FileChr };

/** RAM 的大小取自文件头的哪一项。 */
enum class HeaderRam { PrgRam, PrgNvram, PrgAny, ChrRam, ChrNvram };

/**
 * RAM 的大小取自文件头：头里写明了就用头里的；没写明（旧 iNES 头）时，PrgRam 在文件带电池时为 0、否则取 defaultSize，
 * PrgNvram 在文件带电池时取 defaultSize、否则为 0，ChrRam 取 defaultSize，ChrNvram 为 0。
 * PrgAny 是卡带默认挂在 $6000 的那块 RAM：PrgNvram 的结果不为 0 时用它（带电池），否则用 PrgRam 的结果。
 */
struct HeaderSize {
  HeaderRam field{HeaderRam::PrgRam};
  std::uint32_t defaultSize{0};
};

/** 来源为空时改用的 RAM。 */
struct FallbackRam {
  /** 固定大小；size 取自文件头时是 header 里的默认大小。 */
  std::uint32_t size{0};
  bool battery{false};
  std::optional<HeaderSize> header;
};

/** 存储区声明。 */
struct StorageDecl {
  std::string id;
  bool ram{false};
  StorageSource from{StorageSource::None};
  /** 固定大小；size 取自文件头时是 header 里的默认大小。 */
  std::uint32_t size{0};
  bool battery{false};
  std::optional<FallbackRam> fallback;
  /** 只用于 ram：大小取自文件头。 */
  std::optional<HeaderSize> header;
};

/** 窗口复位后指向的 bank 的写法。 */
enum class InitialKind { Bank, First, Last };

/** 窗口声明。 */
struct WindowDecl {
  std::string id;
  bool ppu{false};
  AddressRange range;
  /** 复位后绑定的存储区（storageChoices 的第一项）。 */
  std::string storage;
  /** 窗口可以切换到的全部存储区，第一项是 storage；只写一个存储区时只有它。 */
  std::vector<std::string> storageChoices;
  std::uint32_t size{0};
  InitialKind initialKind{InitialKind::First};
  std::int64_t initialBank{0};
  bool writable{false};
};

/** 名称表的初始连法：custom 表示四个 1K 槽由脚本逐个映射到主板显存的两页或卡带存储区的某个 1K 页。 */
enum class NametableMode { Header, Horizontal, Vertical, Single0, Single1, Four, Custom };

/** 读寄存器的处理方式：回调脚本，或由宿主直接回答。 */
enum class ReadKind { None, Script, Value, Open, State };

/** 读寄存器的声明：Value 驱动固定值，Open 不驱动总线，State 驱动状态的低 8 位；releaseIrq 为真时读完撤销 IRQ。 */
struct ReadAction {
  ReadKind kind{ReadKind::None};
  std::uint8_t value{0};
  std::string state;
  bool releaseIrq{false};
};

/** 把写入的数据取出若干位送给窗口当 bank 号：(数据 >> shift) & mask。 */
struct BankSetter {
  std::string window;
  std::uint32_t shift{0};
  std::uint32_t mask{0xFF};
};

/** 写寄存器的声明：回调脚本，或由宿主直接执行；宿主执行时依次做 state、banks、update，或者把写入转给电路（circuit，与前面几项互斥）。 */
struct WriteAction {
  bool script{false};
  /** 非空时把写入转给这个标识的电路；电路写完后按它的 wire 自动设置窗口，脚本导出了 onCircuit 就再回调它。 */
  std::string circuit;
  /** 非空时把写入的数据存进这个状态。 */
  std::string state;
  std::vector<BankSetter> banks;
  /** 非空时按这张表的索引查表，结果存进 updateInto 状态。 */
  std::string updateTable;
  std::string updateInto;
};

/** 寄存器范围声明：落在范围内的读写由脚本或宿主处理。read、write 表示这个方向有处理（脚本或宿主）。 */
struct RegisterDecl {
  bool ppu{false};
  AddressRange range;
  bool read{false};
  bool write{false};
  ReadAction readAction;
  WriteAction writeAction;
};

/** 脚本状态声明。initial 写出时，每次复位（含软复位）回到这个值；没写的只在上电时清零。 */
struct StateDecl {
  std::string name;
  std::uint32_t bits{0};
  std::string description;
  std::optional<std::uint32_t> initial;
};

/** 查表索引的一个来源：状态、访问地址的若干位、写入数据的若干位、磁带数据线的电平。 */
enum class IndexFrom { State, Address, Data, Tape };

struct IndexSource {
  IndexFrom from{IndexFrom::State};
  std::string state;
  std::uint32_t shift{0};
  /** 占的位数；状态来源由清单检查时取状态的位宽。 */
  std::uint32_t bits{0};
};

/** 查表声明：索引由各来源按顺序拼成（第一个来源在高位），表项由脚本导出的 tables.<id> 计算，宿主按索引缓存。 */
struct TableDecl {
  std::string id;
  std::vector<IndexSource> index;
  std::uint32_t indexBits{0};
  /** 表项的位宽，结果按它截断。 */
  std::uint32_t bits{0};
};

/** 磁带数据线的比较器。 */
struct TapeComparatorDecl {
  std::int32_t low{0};
  std::int32_t high{0};
  bool invert{false};
};

/** 磁带边沿计时：每次翻转把采样数累加进 samples，下降沿时把它按 分子/分母 折成计时加进 ticks、清零 samples，irq 为真时拉低 IRQ。 */
struct TapeTimerDecl {
  std::string samples;
  std::string ticks;
  std::uint32_t scaleNumerator{1};
  std::uint32_t scaleDenominator{1};
  bool irq{false};
};

/** 磁带声明。 */
struct TapeDecl {
  std::optional<TapeComparatorDecl> comparator;
  std::optional<TapeTimerDecl> timer;
};

/** 电路写完后由宿主自动设置的窗口：程序页、图案页按电路给出的页号逐槽设置，镜像位设置名称表，PRG RAM 保护位设置 RAM 窗口的访问。 */
struct CircuitWire {
  /** 从 $8000 起每个程序页槽对应的窗口（按电路的页大小连续排列）。 */
  std::vector<std::string> prg;
  /** 从 $0000 起每个图案页槽对应的窗口。 */
  std::vector<std::string> chr;
  /** 把电路的镜像式设给名称表。 */
  bool mirroring{false};
  /** 非空时按电路的 PRG RAM 保护位（位 7 允许、位 6 写保护）设置这个 CPU 窗口：不允许时取消映射，写保护时只读。 */
  std::string ram;
};

/** 电路声明：引用主机里已有的 mapper 电路（如 mmc3），电路的内部不能改，插件只决定接线。 */
struct CircuitDecl {
  std::string id;
  std::string type;
  bool revA{false};
  std::optional<CircuitWire> wire;
};

/** 设置项种类。 */
enum class SettingKind { Text, Choice, Switch };

/** choice 设置项的选项。 */
struct SettingOption {
  std::string value;
  std::string label;
};

/** 设置项声明（后续阶段才有运行时行为）。 */
struct SettingDecl {
  std::string key;
  std::string label;
  SettingKind kind{SettingKind::Text};
  /** text 与 choice 的默认值。 */
  std::string textDefault;
  /** switch 的默认值。 */
  bool switchDefault{false};
  std::string description;
  std::vector<SettingOption> options;
};

/** 插件清单。 */
struct Manifest {
  std::string id;
  std::string name;
  std::string version;
  Version versionNumber;
  std::string publisher;
  std::string description;
  std::uint32_t apiVersion{0};
  std::vector<std::string> host;
  /** 卡带用到的外设（目前只有 mouse）；不写等于没有。 */
  std::vector<std::string> devices;
  /** 寄存器写入的数据是否与该地址当前读到的 ROM 字节相与（总线冲突）的初始值，脚本可以用 ctx.busConflict.set 改。 */
  bool busConflict{false};
  std::vector<MatchRule> match;
  std::vector<StorageDecl> storage;
  std::vector<WindowDecl> windows;
  NametableMode nametable{NametableMode::Header};
  std::vector<RegisterDecl> registers;
  std::vector<StateDecl> state;
  std::vector<TableDecl> tables;
  std::optional<TapeDecl> tape;
  std::vector<CircuitDecl> circuits;
  std::vector<SettingDecl> settings;

  /** 有读寄存器要回调脚本。 */
  [[nodiscard]] bool needsReadHandler() const noexcept;
  /** 有写寄存器要回调脚本。 */
  [[nodiscard]] bool needsWriteHandler() const noexcept;
  /** 声明了查表（表项由脚本计算）。 */
  [[nodiscard]] bool needsTables() const noexcept { return !tables.empty(); }
};

/**
 * 解析并检查清单。
 * @param text manifest.json 的内容。
 * @throws shared::CoreError mappers.invalidPackage，说明里指出出错的字段。
 */
[[nodiscard]] Manifest parseManifest(std::string_view text);

/** 地址范围的文字，如 $8000-$9FFF；单个地址写成 $4100。 */
[[nodiscard]] std::string formatRange(const AddressRange& range);

/** 名称表连法在清单里的名称：header、horizontal、vertical、single0、single1、four。 */
[[nodiscard]] std::string_view nametableName(NametableMode mode) noexcept;

}  // namespace ember::infra::mappers
