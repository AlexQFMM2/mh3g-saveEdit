#!/usr/bin/env python3
"""Add user-owned MH3G weapon models to a private portable package.

This tool intentionally writes only below the supplied package directory.
The generated resources are excluded from Git and are never used by CI.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import struct
import zlib
from pathlib import Path


FOLDERS = ("w00", "w01", "w02", "w03", "w04", "w06", "w07", "w08", "w09", "w10", "w11", "w12")
TYPE_MAGIC_VERSION = {
    0x58A15856: (b"MOD\0", 0xE6),
    0x241F5DEB: (b"TEX\0", 0xA5),
    0x2749C8A8: (b"MRL\0", 0x20),
}


def looks_like_root(path: Path) -> bool:
    return path.is_dir() and all((path / folder).is_dir() for folder in FOLDERS)


def locate_root(selected: Path) -> Path:
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


def package(source: Path, package_root: Path) -> Path:
    source_root = locate_root(source)
    package_root = package_root.resolve()
    executable = package_root / "MH3USaveEditorGUI.exe"
    if not executable.is_file():
        raise ValueError(f"portable 包内找不到 {executable.name}: {package_root}")

    relative_files = [Path(folder) / name for folder in FOLDERS
                      for name in sorted(item.name for item in (source_root / folder).glob("*.arc"))]
    if len(relative_files) != 558:
        raise ValueError(f"武器 ARC 数量应为 558，实际为 {len(relative_files)}")

    parent = package_root / "resources" / "mh3g" / "weapon-mod"
    target = parent / "v1"
    staging = parent / "v1.packaging"
    previous = parent / "v1.old"
    if staging.exists():
        shutil.rmtree(staging)
    if previous.exists():
        shutil.rmtree(previous)
    staging.mkdir(parents=True)

    manifest_files: list[dict[str, object]] = []
    try:
        for relative in relative_files:
            source_file = source_root / relative
            validate_arc(source_file)
            destination = staging / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source_file, destination)
            manifest_files.append({
                "path": relative.as_posix(),
                "bytes": destination.stat().st_size,
                "sha256": sha256(destination),
            })
        manifest = {
            "arc_count": 558,
            "files": manifest_files,
            "format": "mh3g-weapon-resources-v1",
            "game": "mh3g",
        }
        (staging / "manifest.json").write_text(
            json.dumps(manifest, ensure_ascii=False, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
            newline="\n",
        )
        if target.exists():
            os.replace(target, previous)
        os.replace(staging, target)
        if previous.exists():
            shutil.rmtree(previous)
    except Exception:
        if staging.exists():
            shutil.rmtree(staging)
        if previous.exists() and not target.exists():
            os.replace(previous, target)
        raise
    return target


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True, type=Path, help="MH3G arc/weapon/mod、romfs 或其父目录")
    parser.add_argument("--package", required=True, type=Path, help="包含 MH3USaveEditorGUI.exe 的 portable 目录")
    args = parser.parse_args()
    target = package(args.source, args.package)
    print(json.dumps({"arc_count": 558, "bytes": sum(path.stat().st_size for path in target.rglob("*.arc")),
                      "target": str(target)}, ensure_ascii=False, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
