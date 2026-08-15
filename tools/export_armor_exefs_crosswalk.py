#!/usr/bin/env python3
"""Export MH3G armor-to-player-model mappings from the audited ExeFS .code.

The generated CSV is committed and consumed by the deterministic encyclopedia
builder.  The editor never reads the executable at runtime.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
from collections import defaultdict
from pathlib import Path


EXPECTED_SHA256 = "5374eaac8de5395f346933c4523019a6f643b72e3a73778ccf9a2ac4c32aaa1d"
LOAD_BASE = 0x00100000
RECORD_SIZE = 24
TABLES = {
    5: (0x00B9C400, 380),  # head
    1: (0x00B9E7A0, 382),  # chest
    2: (0x00BA0B70, 363),  # arms
    3: (0x00BA2D78, 371),  # waist
    4: (0x00BA5040, 377),  # legs
}
FIELDS = [
    "dex_id", "save_type", "save_id", "male_model_id", "female_model_id",
    "mapping_status", "mapping_source",
]


def read_csv(path: Path) -> list[dict[str, str]]:
    with path.open("r", encoding="utf-8-sig", newline="") as handle:
        return list(csv.DictReader(handle))


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def record(code: bytes, save_type: int, save_id: int) -> bytes:
    address, count = TABLES[save_type]
    if save_id <= 0 or save_id >= count:
        raise ValueError(f"save type {save_type}: ID {save_id} exceeds table length {count}")
    offset = address - LOAD_BASE + save_id * RECORD_SIZE
    value = code[offset:offset + RECORD_SIZE]
    if len(value) != RECORD_SIZE:
        raise ValueError(f"save type {save_type}: record {save_id} is outside .code")
    return value


def export(code_path: Path, dex_armor_path: Path, members_path: Path) -> list[dict[str, object]]:
    code = code_path.read_bytes()
    if digest(code) != EXPECTED_SHA256:
        raise ValueError("MH3G ExeFS .code SHA-256 does not match the audited build")
    attributes = {int(row["Amr_ID"]): row for row in read_csv(dex_armor_path)}
    members = {int(row["dex_id"]): row for row in read_csv(members_path)}
    if sorted(attributes) != list(range(1, 1652)) or sorted(members) != list(range(1, 1652)):
        raise ValueError("Dex attributes and armor members must both cover IDs 1..1651")

    confirmed: dict[int, tuple[int, int]] = {}
    by_appearance: dict[str, list[tuple[int, int]]] = defaultdict(list)
    for dex_id, member in sorted(members.items()):
        if not member["save_id"]:
            continue
        raw = record(code, int(member["save_type"]), int(member["save_id"]))
        male, female = raw[2], raw[3]
        gender = member["gender"]
        if gender == "both" and male != female:
            raise ValueError(f"armor {dex_id}: unisex model IDs differ ({male}/{female})")
        if gender == "male" and (male == 0 or female != 0):
            raise ValueError(f"armor {dex_id}: invalid male-only model IDs ({male}/{female})")
        if gender == "female" and (female == 0 or male != 0):
            raise ValueError(f"armor {dex_id}: invalid female-only model IDs ({male}/{female})")
        confirmed[dex_id] = (male, female)
        by_appearance[attributes[dex_id]["Amr_ImgFile"]].append((male, female))

    rows: list[dict[str, object]] = []
    for dex_id, member in sorted(members.items()):
        save_id = int(member["save_id"]) if member["save_id"] else None
        if dex_id in confirmed:
            male, female = confirmed[dex_id]
            status = "confirmed_exefs"
            source = "mh3g-exefs-armor-table-record-2-3"
        else:
            candidates = set(by_appearance[attributes[dex_id]["Amr_ImgFile"]])
            chosen: tuple[int, int] | None = None
            if len(candidates) == 1:
                chosen = next(iter(candidates))
            elif member["gender"] == "male":
                gender_candidates = {pair for pair in candidates if pair[0] != 0 and pair[1] == 0}
                if len(gender_candidates) == 1:
                    chosen = next(iter(gender_candidates))
            elif member["gender"] == "female":
                gender_candidates = {pair for pair in candidates if pair[0] == 0 and pair[1] != 0}
                if len(gender_candidates) == 1:
                    chosen = next(iter(gender_candidates))
            if chosen is None:
                male = female = None
                status = "unmapped"
                source = "unmapped"
            else:
                male, female = chosen
                status = "exact_shared_appearance"
                source = "dex-image-key-to-confirmed-exefs-model"
        rows.append({
            "dex_id": dex_id,
            "save_type": int(member["save_type"]),
            "save_id": "" if save_id is None else save_id,
            "male_model_id": "" if male is None else male,
            "female_model_id": "" if female is None else female,
            "mapping_status": status,
            "mapping_source": source,
        })

    counts = {status: sum(row["mapping_status"] == status for row in rows)
              for status in ("confirmed_exefs", "exact_shared_appearance", "unmapped")}
    if counts != {"confirmed_exefs": 1600, "exact_shared_appearance": 31, "unmapped": 20}:
        raise ValueError(f"unexpected armor model coverage: {counts}")
    return rows


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--code", required=True, type=Path)
    parser.add_argument("--dex-armor", required=True, type=Path)
    parser.add_argument("--members", type=Path, default=Path(__file__).with_name("mh3g_armor_set_members.csv"))
    parser.add_argument("--output", type=Path, default=Path(__file__).with_name("mh3g_armor_exefs_crosswalk.csv"))
    args = parser.parse_args()
    rows = export(args.code, args.dex_armor, args.members)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=FIELDS, lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)
    print("armor model crosswalk: exefs=1600 shared=31 unmapped=20")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
