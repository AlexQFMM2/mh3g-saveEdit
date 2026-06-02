#!/usr/bin/env python3
"""Remove explicit placeholder rows from data CSV files without renumbering IDs."""

from __future__ import annotations

import csv
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DATA_DIR = ROOT / "data"
LANGS = ("en", "cn")
FIELDS = ("id", "name", "english", "source")


def is_placeholder(row: dict[str, str]) -> bool:
    name = row.get("name", "").strip()
    english = row.get("english", "").strip()
    source = row.get("source", "").strip()

    return (
        not name
        or source == "placeholder"
        or english == "DUMMY"
        or english.startswith("DUMMY ")
    )


def read_rows(path: Path) -> list[dict[str, str]]:
    if not path.exists():
        return []
    with path.open(encoding="utf-8", newline="") as fh:
        return list(csv.DictReader(fh))


def write_rows(path: Path, rows: list[dict[str, str]]) -> None:
    with path.open("w", encoding="utf-8", newline="") as fh:
        writer = csv.DictWriter(fh, fieldnames=FIELDS)
        writer.writeheader()
        for row in rows:
            writer.writerow({field: row.get(field, "") for field in FIELDS})


def table_names() -> list[str]:
    names: set[str] = set()
    for lang in LANGS:
        names.update(path.stem for path in (DATA_DIR / lang).glob("*.csv"))
    return sorted(names)


def main() -> int:
    total_removed = 0

    for table in table_names():
        placeholder_ids: set[str] = set()
        rows_by_lang: dict[str, list[dict[str, str]]] = {}

        for lang in LANGS:
            path = DATA_DIR / lang / f"{table}.csv"
            rows = read_rows(path)
            rows_by_lang[lang] = rows
            for row in rows:
                if is_placeholder(row):
                    placeholder_ids.add(row.get("id", ""))

        placeholder_ids.discard("")
        if not placeholder_ids:
            continue

        print(f"{table}: pruning {len(placeholder_ids)} ids")
        for lang, rows in rows_by_lang.items():
            path = DATA_DIR / lang / f"{table}.csv"
            if not path.exists():
                continue
            kept = [row for row in rows if row.get("id", "") not in placeholder_ids]
            total_removed += len(rows) - len(kept)
            write_rows(path, kept)

    print(f"removed_rows={total_removed}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
