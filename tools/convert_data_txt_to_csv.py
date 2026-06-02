#!/usr/bin/env python3
"""Convert legacy line-numbered data/*.txt files to explicit-id CSV files."""

from __future__ import annotations

import csv
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DATA_DIR = ROOT / "data"
LANGS = ("en", "cn")
HEADER = ("id", "name", "english", "source")


def read_lines(path: Path) -> list[str]:
    if not path.exists():
        return []
    return path.read_text(encoding="utf-8").splitlines()


def split_localized_name(value: str, english: str) -> str:
    if not value or not english:
        return value

    suffix = f" ({english})"
    if value.endswith(suffix):
        return value[: -len(suffix)].rstrip()

    return value


def convert_file(lang: str, txt_path: Path, report: list[str]) -> None:
    stem = txt_path.stem
    if stem == "items_sources":
        return

    lang_dir = DATA_DIR / lang
    csv_path = lang_dir / f"{stem}.csv"
    lines = read_lines(txt_path)
    en_lines = read_lines(DATA_DIR / "en" / f"{stem}.txt")
    source_lines = read_lines(DATA_DIR / "cn" / "items_sources.txt") if lang == "cn" and stem == "items" else []

    rows: list[dict[str, str]] = []
    ids_seen: set[int] = set()
    empty_names: list[int] = []
    undetached_suffixes: list[int] = []

    if lang != "en" and en_lines and len(lines) != len(en_lines):
        report.append(f"line-count-mismatch,{lang}/{txt_path.name},{len(lines)},{len(en_lines)}")

    for index, raw_name in enumerate(lines, start=1):
        english = en_lines[index - 1] if index - 1 < len(en_lines) else ""
        name = raw_name
        if lang == "cn":
            name = split_localized_name(raw_name, english)
            if english and raw_name.endswith(")") and raw_name == name:
                undetached_suffixes.append(index)
        elif lang == "en":
            english = raw_name

        source = source_lines[index - 1].strip() if index - 1 < len(source_lines) else ""

        if index in ids_seen:
            report.append(f"duplicate-id,{lang}/{csv_path.name},{index}")
        ids_seen.add(index)

        if not name:
            empty_names.append(index)

        rows.append(
            {
                "id": str(index),
                "name": name,
                "english": english,
                "source": source,
            }
        )

    with csv_path.open("w", encoding="utf-8", newline="") as fh:
        writer = csv.DictWriter(fh, fieldnames=HEADER)
        writer.writeheader()
        writer.writerows(rows)

    if empty_names:
        report.append(f"empty-name,{lang}/{csv_path.name},{' '.join(map(str, empty_names[:20]))}")
    if undetached_suffixes:
        report.append(f"suffix-not-detached,{lang}/{csv_path.name},{' '.join(map(str, undetached_suffixes[:20]))}")


def main() -> int:
    report = ["kind,file,detail,expected"]

    for lang in LANGS:
        lang_dir = DATA_DIR / lang
        for txt_path in sorted(lang_dir.glob("*.txt")):
            convert_file(lang, txt_path, report)

    report_path = ROOT / "dump" / "txt_to_csv_report.csv"
    report_path.parent.mkdir(parents=True, exist_ok=True)
    report_path.write_text("\n".join(report) + "\n", encoding="utf-8")
    print(f"report={report_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
