#!/usr/bin/env python3
"""Build the unified MH3G weapon and armor resource-pack Release Asset.

The ZIP contains only the fixed ``resources/`` tree consumed by the editor.
It is intentionally generated outside Git and can be combined with any
compatible program build by extracting both archives into the same folder.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import os
import struct
import zlib
import zipfile
from pathlib import Path


FOLDERS = ("w00", "w01", "w02", "w03", "w04", "w06", "w07", "w08", "w09", "w10", "w11", "w12")
ARMOR_COUNTS = {"f": 1009, "m": 995}
CHARACTER_COUNTS = {"f": 25, "m": 25}
ARMOR_MODEL_CSV = Path(__file__).with_name("mh3g_armor_model_resources.csv")
TYPE_MAGIC_VERSION = {
    0x58A15856: (b"MOD\0", 0xE6),
    0x241F5DEB: (b"TEX\0", 0xA5),
    0x2749C8A8: (b"MRL\0", 0x20),
}


def looks_like_root(path: Path) -> bool:
    return path.is_dir() and all((path / folder).is_dir() for folder in FOLDERS)


def locate_weapon_root(selected: Path) -> Path:
    selected = selected.resolve()
    candidates = (
        selected,
        selected / "arc" / "weapon" / "mod",
        selected / "weapon" / "mod",
        selected / "romfs" / "arc" / "weapon" / "mod",
        selected / "cci_unpacked" / "romfs" / "arc" / "weapon" / "mod",
    )
    for candidate in candidates:
        if looks_like_root(candidate):
            return candidate
    raise ValueError("找不到包含 12 个 wXX 目录的 arc/weapon/mod")


def locate_armor_root(selected: Path) -> Path:
    selected = selected.resolve()
    candidates = (
        selected,
        selected / "player" / "mod",
        selected / "arc" / "player" / "mod",
        selected / "romfs" / "arc" / "player" / "mod",
        selected / "cci_unpacked" / "romfs" / "arc" / "player" / "mod",
    )
    for candidate in candidates:
        if (candidate / "f" / "pl000").is_dir() and (candidate / "m" / "pl000").is_dir():
            return candidate
    raise ValueError("找不到 romfs/arc/player/mod/{f,m}/plXXX")


def validate_arc(path: Path) -> None:
    data = path.read_bytes()
    if len(data) < 12 or data[:4] != b"ARC\0":
        raise ValueError(f"{path.name}: ARC magic 无效")
    version, count = struct.unpack_from("<HH", data, 4)
    if version != 0x10 or count == 0 or 12 + count * 80 > len(data):
        raise ValueError(f"{path.name}: ARC v0x10 目录无效")
    found = {key: 0 for key in TYPE_MAGIC_VERSION}
    for index in range(count):
        base = 12 + index * 80
        type_hash, compressed_size, packed_size, offset = struct.unpack_from("<IIII", data, base + 64)
        unpacked_size = packed_size & 0x3FFFFFFF
        if offset > len(data) or compressed_size > len(data) - offset or unpacked_size > 256 * 1024 * 1024:
            raise ValueError(f"{path.name}: ARC 条目越界")
        try:
            entry = zlib.decompress(data[offset:offset + compressed_size])
        except zlib.error as exc:
            raise ValueError(f"{path.name}: zlib 解压失败: {exc}") from exc
        if len(entry) != unpacked_size:
            raise ValueError(f"{path.name}: ARC 解压尺寸不匹配")
        expected = TYPE_MAGIC_VERSION.get(type_hash)
        if expected:
            magic, entry_version = expected
            if len(entry) < 6 or entry[:4] != magic or struct.unpack_from("<H", entry, 4)[0] != entry_version:
                raise ValueError(f"{path.name}: {magic[:3].decode()} 版本校验失败")
            found[type_hash] += 1
    if not all(found.values()):
        raise ValueError(f"{path.name}: 缺少 MOD、TEX 或 MRL")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def zip_info(path: str) -> zipfile.ZipInfo:
    info = zipfile.ZipInfo(path, (1980, 1, 1, 0, 0, 0))
    info.compress_type = zipfile.ZIP_STORED
    info.external_attr = 0o100644 << 16
    return info


def build_resource_pack(weapon_source: Path, armor_source: Path, output: Path) -> Path:
    weapon_root = locate_weapon_root(weapon_source)
    armor_root = locate_armor_root(armor_source)
    source_files: list[tuple[Path, Path, str]] = []
    for folder in FOLDERS:
        for item in sorted((weapon_root / folder).glob("*.arc")):
            source_files.append((item, Path("weapon-mod") / folder / item.name, "weapon"))
    weapon_count = sum(kind == "weapon" for _, _, kind in source_files)
    if weapon_count != 558:
        raise ValueError(f"武器 ARC 数量应为 558，实际为 {weapon_count}")
    armor_counts: dict[str, int] = {}
    for gender in ("f", "m"):
        before = len(source_files)
        for directory in sorted((armor_root / gender).glob("pl[0-9][0-9][0-9]")):
            for item in sorted(directory.glob("*.arc")):
                source_files.append((item, Path("armor-mod") / gender / directory.name / item.name, f"armor-{gender}"))
        armor_counts[gender] = len(source_files) - before
        if armor_counts[gender] != ARMOR_COUNTS[gender]:
            raise ValueError(f"{gender} 防具 ARC 数量应为 {ARMOR_COUNTS[gender]}，实际为 {armor_counts[gender]}")
    character_counts: dict[str, int] = {}
    for gender in ("f", "m"):
        before = len(source_files)
        for kind, count in (("face", 11), ("hair", 14)):
            for variant in range(count):
                directory = armor_root / gender / f"{kind}{variant:03d}"
                expected = directory / f"{gender}_{kind}{variant:03d}.arc"
                if not expected.is_file():
                    raise ValueError(f"缺少人物资源: {expected}")
                source_files.append((expected,
                    Path("character-mod") / gender / directory.name / expected.name,
                    f"character-{gender}"))
        character_counts[gender] = len(source_files) - before
        if character_counts[gender] != CHARACTER_COUNTS[gender]:
            raise ValueError(f"{gender} 人物 ARC 数量应为 {CHARACTER_COUNTS[gender]}，实际为 {character_counts[gender]}")
    with ARMOR_MODEL_CSV.open("r", encoding="utf-8-sig", newline="") as handle:
        expected_armor_paths = {row["arc_relative_path"] for row in csv.DictReader(handle)}
    actual_armor_paths = {relative.as_posix() for _, relative, kind in source_files if kind.startswith("armor-")}
    if actual_armor_paths != expected_armor_paths:
        missing = sorted(expected_armor_paths - actual_armor_paths)[:5]
        extra = sorted(actual_armor_paths - expected_armor_paths)[:5]
        raise ValueError(f"防具资源与显式模型表不一致；缺少={missing} 额外={extra}")

    manifest_files: list[dict[str, object]] = []
    for source_file, relative, kind in source_files:
        validate_arc(source_file)
        manifest_files.append({
            "path": relative.as_posix(),
            "bytes": source_file.stat().st_size,
            "sha256": sha256(source_file),
            "kind": kind,
        })
    manifest = {
        "arc_count": len(source_files),
        "counts": {"weapon": weapon_count, "armor_female": armor_counts["f"], "armor_male": armor_counts["m"],
                   "character_female": character_counts["f"], "character_male": character_counts["m"]},
        "files": manifest_files,
        "format": "mh3g-resources-v3",
        "game": "mh3g",
        "armor_model_inventory_sha256": sha256(ARMOR_MODEL_CSV),
    }
    manifest_data = (json.dumps(manifest, ensure_ascii=False, indent=2, sort_keys=True) + "\n").encode("utf-8")

    output = output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    staging = output.with_name(output.name + ".packaging")
    if staging.exists():
        staging.unlink()
    prefix = "resources/mh3g/v3/"
    try:
        with zipfile.ZipFile(staging, "w", allowZip64=True) as archive:
            archive.writestr(zip_info(prefix + "manifest.json"), manifest_data)
            for source_file, relative, _ in source_files:
                archive.writestr(zip_info(prefix + relative.as_posix()), source_file.read_bytes())
        os.replace(staging, output)
    except Exception:
        if staging.exists():
            staging.unlink()
        raise
    return output


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--weapon-source", required=True, type=Path, help="MH3G arc/weapon/mod、romfs 或其父目录")
    parser.add_argument("--armor-source", required=True, type=Path, help="MH3G arc/player/mod、romfs 或其父目录")
    parser.add_argument("--output", required=True, type=Path, help="输出的独立资源包 ZIP")
    args = parser.parse_args()
    target = build_resource_pack(args.weapon_source, args.armor_source, args.output)
    print(json.dumps({"arc_count": 2612, "bytes": target.stat().st_size,
                      "output": str(target)}, ensure_ascii=False, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
