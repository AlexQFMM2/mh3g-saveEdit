#!/usr/bin/env python3
"""Validate the manually maintained MH3G armor-set tables."""

from __future__ import annotations

import argparse
import csv
import re
from collections import Counter, defaultdict
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SET_FIELDS = [
    "set_id", "rank", "combat", "model_id", "name_cn", "name_en",
    "display_order", "review_status", "source", "notes",
]
MEMBER_FIELDS = [
    "set_id", "part", "dex_id", "gender", "slot_order", "save_type",
    "save_id", "mapping_source", "notes",
]
PART_TYPES = {"head": 5, "chest": 1, "arms": 2, "waist": 3, "legs": 4}
PART_ORDER = {name: order for order, name in enumerate(PART_TYPES)}


def read_csv(path: Path, expected: list[str]) -> list[dict[str, str]]:
    with path.open("r", encoding="utf-8-sig", newline="") as handle:
        reader = csv.DictReader(handle)
        if reader.fieldnames != expected:
            raise ValueError(f"{path}: columns must be {','.join(expected)}")
        return list(reader)


def require(condition: bool, message: str, errors: list[str]) -> None:
    if not condition:
        errors.append(message)


def validate(sets_path: Path, members_path: Path, models_path: Path) -> tuple[int, int, int]:
    sets = read_csv(sets_path, SET_FIELDS)
    members = read_csv(members_path, MEMBER_FIELDS)
    errors: list[str] = []

    set_ids = [row["set_id"] for row in sets]
    orders = [int(row["display_order"]) for row in sets]
    require(len(set_ids) == len(set(set_ids)), "armor_sets: duplicate set_id", errors)
    require(len(orders) == len(set(orders)), "armor_sets: duplicate display_order", errors)
    require(sorted(orders) == list(range(len(sets))), "armor_sets: display_order must be dense from zero", errors)
    for row in sets:
        label = f"armor_sets {row['set_id']}"
        require(row["rank"] in {"low", "high", "g", "special"}, f"{label}: invalid rank", errors)
        require(row["combat"] in {"both", "blade", "gunner"}, f"{label}: invalid combat", errors)
        require(0 <= int(row["model_id"]) <= 999, f"{label}: invalid model_id", errors)
        require(bool(row["name_cn"].strip()) and bool(row["name_en"].strip()), f"{label}: empty name", errors)
        require(row["review_status"] in {"candidate", "reviewed"}, f"{label}: invalid review_status", errors)
        require(bool(row["source"].strip()), f"{label}: empty source", errors)

    known_sets = set(set_ids)
    dex_ids: list[int] = []
    save_keys: list[tuple[int, int]] = []
    by_set: dict[str, list[dict[str, str]]] = defaultdict(list)
    for row in members:
        label = f"armor_set_members dex_id={row['dex_id']}"
        require(row["set_id"] in known_sets, f"{label}: unknown set {row['set_id']}", errors)
        require(row["part"] in PART_TYPES, f"{label}: invalid part", errors)
        require(row["gender"] in {"both", "male", "female"}, f"{label}: invalid gender", errors)
        dex_id = int(row["dex_id"])
        require(dex_id > 0, f"{label}: invalid Dex ID", errors)
        dex_ids.append(dex_id)
        if row["part"] in PART_TYPES:
            require(int(row["save_type"]) == PART_TYPES[row["part"]], f"{label}: save_type does not match part", errors)
        if row["save_id"]:
            save_id = int(row["save_id"])
            require(save_id > 0, f"{label}: invalid save_id", errors)
            save_keys.append((int(row["save_type"]), save_id))
            require(row["mapping_source"] != "unmapped", f"{label}: mapped row marked unmapped", errors)
        else:
            require(row["mapping_source"] == "unmapped", f"{label}: empty save_id must be unmapped", errors)
        by_set[row["set_id"]].append(row)

    duplicate_dex = [key for key, count in Counter(dex_ids).items() if count > 1]
    duplicate_save = [key for key, count in Counter(save_keys).items() if count > 1]
    require(not duplicate_dex, f"armor_set_members: duplicate Dex IDs {duplicate_dex[:10]}", errors)
    require(not duplicate_save, f"armor_set_members: duplicate save mappings {duplicate_save[:10]}", errors)
    require(sorted(dex_ids) == list(range(1, 1652)), "armor_set_members: must cover every Dex armor ID 1..1651 once", errors)

    sets_by_id = {row["set_id"]: row for row in sets}
    all_parts = set(PART_TYPES)
    for set_id, set_row in sets_by_id.items():
        rows = by_set.get(set_id, [])
        require(bool(rows), f"armor_sets {set_id}: has no members", errors)
        slot_orders = [int(row["slot_order"]) for row in rows]
        require(len(slot_orders) == len(set(slot_orders)), f"armor_sets {set_id}: duplicate slot_order", errors)
        require(sorted(slot_orders) == list(range(len(rows))), f"armor_sets {set_id}: slot_order must be dense", errors)
        for gender in ("male", "female"):
            visible = [row for row in rows if row["gender"] in {"both", gender}]
            parts = [row["part"] for row in visible]
            duplicates = [part for part, count in Counter(parts).items() if count > 1]
            require(not duplicates, f"armor_sets {set_id}: duplicate {gender} parts {duplicates}", errors)
            # A handful of complete sets are female-only (for example Sailor).
            # Validate only the genders for which the table defines members.
            if visible and set_row["rank"] != "special":
                require(set(parts) == all_parts, f"armor_sets {set_id}: incomplete {gender} non-special set", errors)
        expected = sorted(rows, key=lambda row: (
            PART_ORDER.get(row["part"], 99), {"both": 0, "male": 1, "female": 2}.get(row["gender"], 9),
            int(row["dex_id"])))
        require(rows == expected, f"armor_sets {set_id}: member rows are not in stable slot order", errors)

    if errors:
        raise ValueError("armor-set validation failed:\n- " + "\n- ".join(errors))
    with models_path.open("r", encoding="utf-8-sig", newline="") as handle:
        models = list(csv.DictReader(handle))
    require(len(models) == 2004, "armor models: expected 2004 explicit resources", errors)
    require(sum(row["gender"] == "female" for row in models) == 1009, "armor models: expected 1009 female resources", errors)
    require(sum(row["gender"] == "male" for row in models) == 995, "armor models: expected 995 male resources", errors)
    model_keys = [row["model_key"] for row in models]
    model_slots = [(row["model_id"], row["gender"], row["part"]) for row in models]
    require(len(model_keys) == len(set(model_keys)), "armor models: duplicate model_key", errors)
    require(len(model_slots) == len(set(model_slots)), "armor models: duplicate model/gender/part", errors)
    for row in models:
        require(bool(re.fullmatch(r"armor-[fm]-pl\d{3}-(head|chest|arms|waist|legs)", row["model_key"])),
                f"armor models: invalid key {row['model_key']}", errors)
        require(bool(re.fullmatch(r"armor-mod/[fm]/pl\d{3}/[fm]_(helm|body|arm|wst|leg)\d{3}\.arc", row["arc_relative_path"])),
                f"armor models: invalid path {row['arc_relative_path']}", errors)
    if errors:
        raise ValueError("armor-set validation failed:\n- " + "\n- ".join(errors))
    return len(sets), len(members), sum(not row["save_id"] for row in members)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sets", type=Path, default=ROOT / "tools" / "mh3g_armor_sets.csv")
    parser.add_argument("--members", type=Path, default=ROOT / "tools" / "mh3g_armor_set_members.csv")
    parser.add_argument("--models", type=Path, default=ROOT / "tools" / "mh3g_armor_model_resources.csv")
    args = parser.parse_args()
    sets, members, unmapped = validate(args.sets, args.members, args.models)
    print(f"armor sets OK: sets={sets} members={members} unmapped={unmapped}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
