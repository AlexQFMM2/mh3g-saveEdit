#!/usr/bin/env python3
"""Validate an extracted MH3G resources tree before packaging the editor."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath


INVENTORY = Path(__file__).with_name("mh3g_armor_model_resources.csv")


def digest(path: Path) -> str:
    value = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            value.update(block)
    return value.hexdigest()


def validate(package_root: Path) -> None:
    root = package_root.resolve() / "resources" / "mh3g" / "v2"
    manifest_path = root / "manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    files = manifest.get("files")
    if manifest.get("format") != "mh3g-resources-v2" or manifest.get("game") != "mh3g":
        raise ValueError("资源 manifest 格式或游戏版本不匹配")
    if manifest.get("armor_model_inventory_sha256") != digest(INVENTORY):
        raise ValueError("资源包的防具模型映射版本与程序不一致")
    expected_counts = {"weapon": 558, "armor_female": 1009, "armor_male": 995}
    if (manifest.get("arc_count") != 2562 or manifest.get("counts") != expected_counts
            or not isinstance(files, list) or len(files) != 2562):
        raise ValueError("资源 manifest 必须包含 558 个武器、1009 个女性和 995 个男性防具 ARC")

    seen: set[str] = set()
    for entry in files:
        relative = entry.get("path", "")
        parts = PurePosixPath(relative).parts
        weapon_path = (len(parts) == 3 and parts[0] == "weapon-mod" and parts[1] in {
            "w00", "w01", "w02", "w03", "w04", "w06", "w07", "w08", "w09", "w10", "w11", "w12"
        } and entry.get("kind") == "weapon")
        armor_path = (len(parts) == 4 and parts[0] == "armor-mod" and parts[1] in {"f", "m"}
            and parts[2].startswith("pl") and len(parts[2]) == 5
            and entry.get("kind") == f"armor-{parts[1]}")
        if (not relative.endswith(".arc") or not (weapon_path or armor_path)
                or ".." in parts or relative in seen):
            raise ValueError(f"非法或重复的资源路径: {relative}")
        seen.add(relative)
        path = root.joinpath(*parts)
        if not path.is_file() or path.stat().st_size != entry.get("bytes") or digest(path) != entry.get("sha256"):
            raise ValueError(f"资源缺失或哈希不匹配: {relative}")

    actual = {path.relative_to(root).as_posix() for path in root.rglob("*.arc")}
    if actual != seen:
        raise ValueError("资源目录包含 manifest 之外的 ARC，或缺少已登记文件")
    unpacked_formats = [
        path.relative_to(package_root).as_posix()
        for suffix in ("*.mod", "*.tex", "*.mrl")
        for path in package_root.rglob(suffix)
    ]
    if unpacked_formats:
        raise ValueError(f"portable 不应包含解包后的模型或贴图文件: {unpacked_formats[:5]}")
    print(json.dumps({"arc_count": len(seen), "counts": expected_counts, "root": str(root)}, ensure_ascii=False, sort_keys=True))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("package_root", type=Path, help="包含 resources 目录的 portable 根目录")
    args = parser.parse_args()
    validate(args.package_root)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
