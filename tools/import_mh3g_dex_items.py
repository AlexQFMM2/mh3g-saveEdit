#!/usr/bin/env python3
"""Import MH3G Dex item Chinese names into data/cn/items.csv.

This importer matches by English item name instead of row order. The editor's
item list contains historical gaps, typos, and extras, while the Dex dump uses
its own Itm_ID. Reordering data files would affect save IDs, so this script only
updates display names and writes a report with both ID systems for review.
"""

from __future__ import annotations

import csv
import re
from dataclasses import dataclass
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DATA_CN = ROOT / "data/cn/items.csv"
DUMP_DIR = ROOT / "dump"
DEX_CSV = Path("/mnt/DEX/mh3g-dex-dump/dump/mh3g-dex/direct_sql/items_id_zh_en.csv")
REPORT_CSV = DUMP_DIR / "dex_items_import_report.csv"
UNMATCHED_CSV = DUMP_DIR / "dex_items_unmatched.csv"


ALIASES = {
    "Durgged Meat": "Drugged Meat",
    "Slee Knife": "Sleep Knife",
    "Draong S": "Dragon S",
    "Shushifish Bait": "Sushifish Bait",
    "Carbage": "Garbage",
    "Coma Sec": "Coma Sac",
    "Avian Shoutbone": "Avian Stoutbone",
    "Monster slogbne": "Monster Slogbone",
    "Vermilion Scale": "Vermillion Scale",
    "Primshroom Lamp": "Prismshroom Lamp",
    "Model Poogieship": "Model Poggieship",
    "Rathalos Carapace": "Rathlos Carapace",
    "Rathalos Fellwing": "Rathlos Fellwing",
    "H. Jhen Rocksin": "H.Jhen Rockskin",
    "Lagombi Plastron": "Lagom Plastron",
    "Lagombi Plastron +": "Lagom Plastron+",
    "Zin Eletrofur": "Zin Electrofur",
    "Zin Eletrofur +": "Zin Electrofur+",
    "L. Narha Dapples": "L.Narga Dapples",
    "Handicarft Jwl 3": "Handicraft Jwl 3",
    "Supersize Scale": "Supersized Scale",
    "Paracoat Jewel 1": "Paracoat Jwl 1",
    "Paracoat Jewel 2": "Paracoat Jwl 2",
    "Tamzia Chips": "Tanzia Chips",
}


@dataclass
class DexItem:
    itm_id: int
    english: str
    zh_cn: str
    zh_tw: str
    japanese: str


def normalize_name(value: str) -> str:
    value = value.lower().replace("+", " plus ")
    return re.sub(r"[^a-z0-9]+", "", value)


def is_placeholder(english: str) -> bool:
    return english == "DUMMY" or english.startswith("DUMMY ")


def load_editor_items() -> list[dict[str, str]]:
    with DATA_CN.open(encoding="utf-8", newline="") as fh:
        rows = list(csv.DictReader(fh))

    for row in rows:
        row.setdefault("id", "")
        row.setdefault("name", "")
        row.setdefault("english", "")
        row.setdefault("source", "")

    return rows


def write_editor_items(rows: list[dict[str, str]]) -> None:
    with DATA_CN.open("w", encoding="utf-8", newline="") as fh:
        writer = csv.DictWriter(fh, fieldnames=["id", "name", "english", "source"])
        writer.writeheader()
        writer.writerows(rows)


def load_dex_items() -> dict[str, DexItem]:
    by_name: dict[str, DexItem] = {}
    duplicates: dict[str, list[DexItem]] = {}

    with DEX_CSV.open(encoding="utf-8-sig", newline="") as fh:
        for row in csv.DictReader(fh):
            english = row["Itm_Name_0"].strip()
            zh_cn = row["Itm_Name_1"].strip()
            if not english or not zh_cn or english == "---":
                continue

            item = DexItem(
                itm_id=int(row["Itm_ID"]),
                english=english,
                zh_cn=zh_cn,
                zh_tw=row.get("Itm_Name_2", "").strip(),
                japanese=row.get("Itm_Name_3", "").strip(),
            )
            key = normalize_name(english)
            if key in by_name:
                duplicates.setdefault(key, [by_name[key]]).append(item)
            else:
                by_name[key] = item

    for key in duplicates:
        by_name.pop(key, None)

    return by_name


def main() -> int:
    if not DEX_CSV.exists():
        raise SystemExit(f"Dex item dump not found: {DEX_CSV}")

    editor_rows = load_editor_items()
    dex_by_name = load_dex_items()
    report_rows: list[list[str]] = []
    unmatched_rows: list[list[str]] = []
    changed = 0
    exact = 0
    alias = 0
    placeholder = 0
    unmatched = 0

    for row in editor_rows:
        editor_id = int(row["id"])
        english = row.get("english", "")
        current = row.get("name", "")
        if not english:
            report_rows.append([editor_id, "", "", "", current, "", "empty"])
            continue

        if is_placeholder(english):
            placeholder += 1
            row["name"] = f"占位 {english[6:]}".rstrip() if english.startswith("DUMMY ") else "占位"
            row["source"] = "placeholder"
            report_rows.append([editor_id, "", english, "", current, row["name"], "placeholder"])
            continue

        match_kind = "dex-exact"
        dex_item = dex_by_name.get(normalize_name(english))
        if dex_item is None and english in ALIASES:
            dex_item = dex_by_name.get(normalize_name(ALIASES[english]))
            match_kind = "dex-alias"

        if dex_item is None:
            unmatched += 1
            unmatched_rows.append([editor_id, english, current])
            report_rows.append([editor_id, "", english, "", current, current, "unmatched"])
            continue

        new_value = dex_item.zh_cn
        if current != new_value:
            changed += 1
            row["name"] = new_value
        row["source"] = match_kind
        if match_kind == "dex-exact":
            exact += 1
        else:
            alias += 1

        report_rows.append([
            editor_id,
            dex_item.itm_id,
            english,
            dex_item.english,
            current,
            new_value,
            match_kind,
        ])

    write_editor_items(editor_rows)

    DUMP_DIR.mkdir(parents=True, exist_ok=True)
    with REPORT_CSV.open("w", encoding="utf-8", newline="") as fh:
        writer = csv.writer(fh)
        writer.writerow(["editor_id", "dex_itm_id", "editor_english", "dex_english", "old_cn", "new_cn", "source"])
        writer.writerows(report_rows)

    with UNMATCHED_CSV.open("w", encoding="utf-8", newline="") as fh:
        writer = csv.writer(fh)
        writer.writerow(["editor_id", "editor_english", "current_cn"])
        writer.writerows(unmatched_rows)

    print(
        "dex_rows={dex_rows} changed={changed} exact={exact} alias={alias} "
        "placeholder={placeholder} unmatched={unmatched}".format(
            dex_rows=len(dex_by_name),
            changed=changed,
            exact=exact,
            alias=alias,
            placeholder=placeholder,
            unmatched=unmatched,
        )
    )
    print(f"report={REPORT_CSV}")
    print(f"unmatched={UNMATCHED_CSV}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
