#!/usr/bin/env python3
"""Build the standalone MH3G resource-pack Release Asset.

The ZIP contains only the fixed ``resources/`` tree consumed by the editor.
It is intentionally generated outside Git and can be combined with any
compatible program build by extracting both archives into the same folder.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import struct
import zlib
import zipfile
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


def zip_info(path: str) -> zipfile.ZipInfo:
    info = zipfile.ZipInfo(path, (1980, 1, 1, 0, 0, 0))
    info.compress_type = zipfile.ZIP_STORED
    info.external_attr = 0o100644 << 16
    return info


def build_resource_pack(source: Path, output: Path) -> Path:
    source_root = locate_root(source)
    relative_files = [Path(folder) / name for folder in FOLDERS
                      for name in sorted(item.name for item in (source_root / folder).glob("*.arc"))]
    if len(relative_files) != 558:
        raise ValueError(f"武器 ARC 数量应为 558，实际为 {len(relative_files)}")

    manifest_files: list[dict[str, object]] = []
    for relative in relative_files:
        source_file = source_root / relative
        validate_arc(source_file)
        manifest_files.append({
            "path": relative.as_posix(),
            "bytes": source_file.stat().st_size,
            "sha256": sha256(source_file),
        })
    manifest = {
        "arc_count": 558,
        "files": manifest_files,
        "format": "mh3g-weapon-resources-v1",
        "game": "mh3g",
    }
    manifest_data = (json.dumps(manifest, ensure_ascii=False, indent=2, sort_keys=True) + "\n").encode("utf-8")

    output = output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    staging = output.with_name(output.name + ".packaging")
    if staging.exists():
        staging.unlink()
    prefix = "resources/mh3g/weapon-mod/v1/"
    try:
        with zipfile.ZipFile(staging, "w", allowZip64=True) as archive:
            archive.writestr(zip_info(prefix + "manifest.json"), manifest_data)
            for relative in relative_files:
                archive.writestr(zip_info(prefix + relative.as_posix()), (source_root / relative).read_bytes())
        os.replace(staging, output)
    except Exception:
        if staging.exists():
            staging.unlink()
        raise
    return output


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True, type=Path, help="MH3G arc/weapon/mod、romfs 或其父目录")
    parser.add_argument("--output", required=True, type=Path, help="输出的独立资源包 ZIP")
    args = parser.parse_args()
    target = build_resource_pack(args.source, args.output)
    print(json.dumps({"arc_count": 558, "bytes": target.stat().st_size,
                      "output": str(target)}, ensure_ascii=False, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
