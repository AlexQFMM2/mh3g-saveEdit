#!/usr/bin/env python3
"""Export the explicit MH3G armor model inventory used by the encyclopedia."""

from __future__ import annotations

import argparse
import csv
import re
from pathlib import Path


EXPECTED = {"f": 1009, "m": 995}
GENDERS = {"f": "female", "m": "male"}
PARTS = {"helm": "head", "body": "chest", "arm": "arms", "wst": "waist", "leg": "legs"}
FIELDS = ["model_id", "gender", "part", "model_key", "arc_relative_path", "mapping_source"]


def locate(selected: Path) -> Path:
    candidates = (
        selected, selected / "player" / "mod", selected / "arc" / "player" / "mod",
        selected / "romfs" / "arc" / "player" / "mod",
        selected / "cci_unpacked" / "romfs" / "arc" / "player" / "mod",
    )
    for candidate in candidates:
        if (candidate / "f").is_dir() and (candidate / "m").is_dir():
            return candidate.resolve()
    raise ValueError("找不到 romfs/arc/player/mod/{f,m}")


def export(source: Path) -> list[dict[str, object]]:
    root = locate(source)
    rows: list[dict[str, object]] = []
    for short_gender, gender in GENDERS.items():
        count = 0
        pattern = re.compile(rf"{short_gender}_(helm|body|arm|wst|leg)(\d{{3}})\.arc$")
        for directory in sorted((root / short_gender).glob("pl[0-9][0-9][0-9]")):
            model_id = int(directory.name[2:])
            for path in sorted(directory.glob("*.arc")):
                match = pattern.fullmatch(path.name)
                if not match or int(match.group(2)) != model_id:
                    raise ValueError(f"非标准防具 ARC 路径: {path}")
                part = PARTS[match.group(1)]
                rows.append({
                    "model_id": model_id,
                    "gender": gender,
                    "part": part,
                    "model_key": f"armor-{short_gender}-pl{model_id:03d}-{part}",
                    "arc_relative_path": f"armor-mod/{short_gender}/pl{model_id:03d}/{path.name}",
                    "mapping_source": "romfs-player-model-inventory",
                })
                count += 1
        if count != EXPECTED[short_gender]:
            raise ValueError(f"{short_gender} 防具 ARC 应为 {EXPECTED[short_gender]}，实际为 {count}")
    rows.sort(key=lambda row: (int(row["model_id"]), str(row["gender"]), list(PARTS.values()).index(str(row["part"]))))
    return rows


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    rows = export(args.source)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=FIELDS, lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)
    print(f"armor model resources={len(rows)} female={EXPECTED['f']} male={EXPECTED['m']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
