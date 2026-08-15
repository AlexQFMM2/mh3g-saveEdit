#!/usr/bin/env python3
"""Validate an extracted MH3G resources tree before packaging the editor."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath


def digest(path: Path) -> str:
    value = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            value.update(block)
    return value.hexdigest()


def validate(package_root: Path) -> None:
    root = package_root.resolve() / "resources" / "mh3g" / "weapon-mod" / "v1"
    manifest_path = root / "manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    files = manifest.get("files")
    if manifest.get("format") != "mh3g-weapon-resources-v1" or manifest.get("game") != "mh3g":
        raise ValueError("资源 manifest 格式或游戏版本不匹配")
    if manifest.get("arc_count") != 558 or not isinstance(files, list) or len(files) != 558:
        raise ValueError("资源 manifest 必须包含 558 个 ARC")

    seen: set[str] = set()
    for entry in files:
        relative = entry.get("path", "")
        parts = PurePosixPath(relative).parts
        if (not relative.endswith(".arc") or len(parts) != 2 or parts[0] not in {
                "w00", "w01", "w02", "w03", "w04", "w06", "w07", "w08", "w09", "w10", "w11", "w12"
        } or ".." in parts or relative in seen):
            raise ValueError(f"非法或重复的资源路径: {relative}")
        seen.add(relative)
        path = root.joinpath(*parts)
        if not path.is_file() or path.stat().st_size != entry.get("bytes") or digest(path) != entry.get("sha256"):
            raise ValueError(f"资源缺失或哈希不匹配: {relative}")

    actual = {path.relative_to(root).as_posix() for path in root.rglob("*.arc")}
    if actual != seen:
        raise ValueError("资源目录包含 manifest 之外的 ARC，或缺少已登记文件")
    print(json.dumps({"arc_count": len(seen), "root": str(root)}, ensure_ascii=False, sort_keys=True))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("package_root", type=Path, help="包含 resources 目录的 portable 根目录")
    args = parser.parse_args()
    validate(args.package_root)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
