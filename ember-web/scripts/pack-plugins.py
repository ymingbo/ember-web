#!/usr/bin/env python3
"""把 ember 插件源码目录批量打包成 .emap。

为什么需要这一步
----------------
ember 的 PluginRegistry 只认 `.emap` 文件：

    plugin-registry.cpp:37  constexpr const char* PACKAGE_EXTENSION = ".emap";
    plugin-registry.cpp:148 if (it->is_regular_file(error) && isPackageFile(it->path())) ...

而仓库里的 `ember/plugins/` 放的是**源码目录**（`nrom/manifest.json`、`nrom/main.js`），
一个 .emap 都没有。所以 preload 进虚拟文件系统后，扫描时被全部跳过，
注册表为空 → 加载任何卡带都报 `mappers.noMatch（缺少 mapper 号 N 的插件）`。

上游文档 `help/plugin/package.md` 写明：.emap 就是标准 zip，根目录放
manifest.json / main.js / icon.png（可选），文件名区分大小写，不能带外层文件夹。

用法
----
    python3 scripts/pack-plugins.py <源插件目录> <输出目录>

输出目录里每个插件生成一个 `<id>.emap`，id 取自 manifest.json 的 "id" 字段
（与 Upstream 的卸载/替换约定一致）。CMake 的 preload 指向输出目录即可。
"""

import json
import sys
import zipfile
from pathlib import Path

# 包内允许的文件（help/plugin/package.md: 根目录只能有这三个）
ALLOWED_FILES = ("manifest.json", "main.js", "icon.png")


def pack_one(src: Path, out_dir: Path) -> tuple[str, int, str]:
    """打包单个插件目录。返回 (id, 字节数, 状态说明)。"""
    manifest_path = src / "manifest.json"
    if not manifest_path.is_file():
        raise RuntimeError("缺少 manifest.json")

    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as e:
        raise RuntimeError(f"manifest.json 不是合法 JSON: {e}")

    plugin_id = manifest.get("id")
    if not plugin_id or not isinstance(plugin_id, str):
        raise RuntimeError("manifest.json 缺少 id 字段")

    # 只收白名单文件，忽略目录里的其它东西（上游目录很干净，这里是防御）
    files = [f for f in ALLOWED_FILES if (src / f).is_file()]
    if "manifest.json" not in files:
        raise RuntimeError("manifest.json 未被收进包")

    target = out_dir / f"{plugin_id}.emap"
    with zipfile.ZipFile(target, "w", zipfile.ZIP_DEFLATED) as zf:
        for name in files:
            zf.write(src / name, arcname=name)

    size = target.stat().st_size
    return plugin_id, size, "main.js" if "main.js" in files else "仅清单"


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__)
        return 2

    src_root = Path(sys.argv[1]).resolve()
    out_dir = Path(sys.argv[2]).resolve()

    if not src_root.is_dir():
        print(f"源目录不存在: {src_root}")
        return 1

    out_dir.mkdir(parents=True, exist_ok=True)

    # 先清掉旧的 .emap，避免插件删了还残留
    for old in out_dir.glob("*.emap"):
        old.unlink()

    done, failed, total_bytes = 0, [], 0
    for child in sorted(src_root.iterdir()):
        if not child.is_dir():
            continue
        try:
            plugin_id, size, kind = pack_one(child, out_dir)
            done += 1
            total_bytes += size
        except RuntimeError as e:
            failed.append((child.name, str(e)))

    print(f"插件打包完成：{done} 个，合计 {total_bytes / 1024:.0f} KB → {out_dir}")
    if failed:
        print(f"失败 {len(failed)} 个：")
        for name, reason in failed:
            print(f"  {name}: {reason}")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
