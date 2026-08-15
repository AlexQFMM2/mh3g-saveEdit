#!/usr/bin/env python3
"""Generate a reviewable initial MH3G armor-set crosswalk.

The output is a scaffold, not a runtime heuristic.  Generate it outside the
repository, review the diff, then deliberately update the two committed CSV
files.  The encyclopedia builder consumes only those explicit committed rows
and never regroups armor names at runtime.
"""

from __future__ import annotations

import argparse
import csv
import re
import unicodedata
from collections import Counter, defaultdict
from pathlib import Path
from statistics import median


PARTS = {1: (5, "head", "head_armors.csv"), 2: (1, "chest", "chest_armors.csv"),
         3: (2, "arms", "arms_armors.csv"), 4: (3, "waist", "waist_armors.csv"),
         5: (4, "legs", "legs_armors.csv")}
COMBAT = {0: "both", 1: "blade", 2: "gunner"}
GENDER = {0: "both", 1: "male", 2: "female"}
RANK_ORDER = {"low": 0, "high": 1, "g": 2, "special": 3}
CN_PART_SUFFIXES = (
    "头盔", "战帽", "帽子", "头饰", "耳环", "面具", "斗笠", "首饰", "王冠", "头巾", "假面", "伪装",
    "铠甲", "炼甲", "上衣", "背心", "外套", "夹克", "服", "装束", "斗篷",
    "腕甲", "护手", "手套", "长袖", "袖子", "腕轮", "护腕", "手腕",
    "腰甲", "裙甲", "腰带", "扣带", "腰饰", "短裙", "裤袋", "皮带",
    "重靴", "轻靴", "具足", "长裤", "足轮", "护腿", "皮靴", "长靴", "裤", "靴", "鞋", "足",
)
EN_PART_SUFFIXES = (
    "headgear", "headpiece", "snorkel", "helm", "cap", "hat", "mask", "lobos", "earring", "crown",
    "mail", "vest", "suit", "jacket", "coat", "torso", "thorax", "shirt",
    "braces", "guards", "gloves", "sleeves", "cuffs", "gauntlets",
    "faulds", "coat", "waist", "belt", "pouch",
    "greaves", "leggings", "boots", "pants", "trousers", "feet",
)


def read_csv(path: Path) -> list[dict[str, str]]:
    with path.open("r", encoding="utf-8-sig", newline="") as handle:
        return list(csv.DictReader(handle))


def write_csv(path: Path, fields: list[str], values: list[dict[str, object]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields, lineterminator="\n")
        writer.writeheader()
        writer.writerows(values)


def normalized(value: str) -> str:
    value = unicodedata.normalize("NFKC", value).casefold().replace("’", "'")
    return re.sub(r"[\s・·【】\[\]()（）,，:：\"“”'‘’._+-]", "", value)


def tier(rarity: int) -> str:
    if rarity <= 3:
        return "low"
    if rarity <= 7:
        return "high"
    return "g"


def strip_cn(name: str) -> tuple[str, str]:
    marker = ""
    match = re.search(r"[ＳＵＸＺ]$", name)
    if match:
        marker = match.group(0)
        name = name[:-1]
    for suffix in sorted(CN_PART_SUFFIXES, key=len, reverse=True):
        if name.endswith(suffix):
            return name[:-len(suffix)], marker
    return name, marker


def strip_en(name: str) -> tuple[str, str]:
    marker = ""
    match = re.search(r"\s+[SUXZ]$", name, re.IGNORECASE)
    if match:
        marker = match.group(0).strip().upper()
        name = name[:match.start()]
    lower = name.casefold()
    for suffix in sorted(EN_PART_SUFFIXES, key=len, reverse=True):
        token = " " + suffix
        if lower.endswith(token):
            return name[:-len(token)], marker
    return name, marker


def set_name(entries: list[dict[str, object]], language: str) -> str:
    stripped = []
    for entry in entries:
        base, marker = (strip_cn(str(entry["name_cn"])) if language == "cn"
                        else strip_en(str(entry["name_en"])))
        stripped.append((base.strip(), marker))
    bases = Counter(base for base, _ in stripped if base)
    markers = Counter(marker for _, marker in stripped if marker)
    if not bases:
        return str(entries[0]["name_cn" if language == "cn" else "name_en"])
    base = sorted(bases.items(), key=lambda item: (-item[1], len(item[0]), item[0]))[0][0]
    marker = sorted(markers.items(), key=lambda item: (-item[1], item[0]))[0][0] if markers else ""
    return base + ((" " if language == "en" else "") + marker if marker else "")


def choose_common(common: list[dict[str, object]], combat: str, split: bool) -> list[dict[str, object]]:
    if not split:
        return common
    result = []
    grouped: dict[tuple[int, int], list[dict[str, object]]] = defaultdict(list)
    for entry in common:
        grouped[(int(entry["part_id"]), int(entry["gender_id"]))].append(entry)
    for values in grouped.values():
        values.sort(key=lambda entry: (-int(entry["defense"]), int(entry["dex_id"])))
        if len(values) == 1:
            result.extend(values)
        elif combat == "blade":
            result.append(values[0])
        else:
            result.append(values[-1])
    return result


def split_extra_variants(
    selected: list[dict[str, object]],
) -> tuple[list[dict[str, object]], list[dict[str, object]]]:
    """Keep one member per gender/part and return unrelated shared-model extras.

    A few standalone pieces reuse a complete set's model group.  The Chakra
    set is the known example: Sword Saint and Barrage earrings share pl115.
    Select the candidate closest to the other members' Dex-ID run for the
    complete row and emit the remaining pieces as independent special rows.
    This is candidate generation only; the checked-in table remains editable.
    """
    grouped: dict[tuple[int, int], list[dict[str, object]]] = defaultdict(list)
    for entry in selected:
        grouped[(int(entry["part_id"]), int(entry["gender_id"]))].append(entry)
    anchors = [int(values[0]["dex_id"]) for values in grouped.values() if len(values) == 1]
    center = median(anchors or [int(entry["dex_id"]) for entry in selected])
    kept: list[dict[str, object]] = []
    extras: list[dict[str, object]] = []
    for values in grouped.values():
        values.sort(key=lambda entry: (abs(int(entry["dex_id"]) - center), int(entry["dex_id"])))
        kept.append(values[0])
        extras.extend(values[1:])
    return kept, extras


def build(dex_root: Path, data_root: Path) -> tuple[list[dict[str, object]], list[dict[str, object]]]:
    armor_rows = read_csv(dex_root / "tables" / "021_u1715.csv")
    names = {int(row["Amr_ID"]): row for row in read_csv(dex_root / "tables" / "022_u1716.csv")}
    first_by_image = {}
    for row in armor_rows:
        first_by_image.setdefault(row["Amr_ImgFile"], int(row["Amr_ID"]))
    model_by_image = {name: index for index, (name, _) in enumerate(
        sorted(first_by_image.items(), key=lambda item: item[1]))}

    save_indexes = {}
    for part_id, (save_type, _, filename) in PARTS.items():
        by_cn: dict[str, list[int]] = defaultdict(list)
        by_en: dict[str, list[int]] = defaultdict(list)
        for row in read_csv(data_root / "cn" / filename):
            save_id = int(row["id"])
            if save_id == 0:
                continue
            by_cn[normalized(row["name"])].append(save_id)
            by_en[normalized(row["english"])].append(save_id)
        save_indexes[part_id] = (save_type, by_cn, by_en)

    prepared = []
    for row in armor_rows:
        dex_id = int(row["Amr_ID"])
        part_id = int(row["Part"])
        name = names[dex_id]
        save_type, by_cn, by_en = save_indexes[part_id]
        matches = by_cn.get(normalized(name["Amr_Name_1"]), [])
        source = "exact-cn"
        if len(matches) != 1:
            matches = by_en.get(normalized(name["Amr_Name_0"]), [])
            source = "exact-en"
        prepared.append({
            "dex_id": dex_id, "image": row["Amr_ImgFile"], "model_id": model_by_image[row["Amr_ImgFile"]],
            "tier": tier(int(row["Rare"])), "combat_id": int(row["BorG"]), "gender_id": int(row["MorF"]),
            "part_id": part_id, "part": PARTS[part_id][1], "save_type": save_type,
            "save_id": matches[0] if len(matches) == 1 else "", "mapping_source": source if len(matches) == 1 else "unmapped",
            "defense": int(row["Def"]), "name_cn": name["Amr_Name_1"], "name_en": name["Amr_Name_0"],
        })

    buckets: dict[tuple[str, str], list[dict[str, object]]] = defaultdict(list)
    for entry in prepared:
        buckets[(str(entry["image"]), str(entry["tier"]))].append(entry)

    set_rows = []
    member_rows = []
    display_order = 0

    def append_set(
        selected: list[dict[str, object]],
        rank: str,
        combat: str,
        notes: str,
    ) -> None:
        nonlocal display_order
        selected = sorted(selected, key=lambda entry: (
            int(entry["part_id"]), int(entry["gender_id"]), int(entry["dex_id"])))
        minimum = min(int(entry["dex_id"]) for entry in selected)
        set_id = f"as-{minimum:04d}-{combat}"
        set_rows.append({
            "set_id": set_id, "rank": rank, "combat": combat,
            "model_id": int(selected[0]["model_id"]),
            "name_cn": set_name(selected, "cn"), "name_en": set_name(selected, "en"),
            "display_order": display_order, "review_status": "candidate",
            "source": "generated-seed", "notes": notes,
        })
        for slot_order, entry in enumerate(selected):
            member_rows.append({
                "set_id": set_id, "part": entry["part"], "dex_id": int(entry["dex_id"]),
                "gender": GENDER[int(entry["gender_id"])], "slot_order": slot_order,
                "save_type": int(entry["save_type"]), "save_id": entry["save_id"],
                "mapping_source": entry["mapping_source"], "notes": "",
            })
        display_order += 1

    for (image, source_tier), bucket in sorted(buckets.items(), key=lambda item: (
            RANK_ORDER[item[0][1]], int(item[1][0]["dex_id"]))):
        specific = sorted({int(entry["combat_id"]) for entry in bucket if int(entry["combat_id"]) in (1, 2)})
        combat_ids = specific or [0]
        common = [entry for entry in bucket if int(entry["combat_id"]) == 0]
        for combat_id in combat_ids:
            combat = COMBAT[combat_id]
            selected = [entry for entry in bucket if int(entry["combat_id"]) == combat_id]
            selected += choose_common(common, combat, len(specific) == 2)
            selected = sorted({int(entry["dex_id"]): entry for entry in selected}.values(),
                              key=lambda entry: (int(entry["part_id"]), int(entry["gender_id"]), int(entry["dex_id"])))
            selected, extras = split_extra_variants(selected)
            parts = {int(entry["part_id"]) for entry in selected}
            rank = source_tier if len(parts) == 5 else "special"
            notes = "generated-review-required" if rank == "special" or any(not entry["save_id"] for entry in selected) else ""
            append_set(selected, rank, combat, notes)
            for extra in sorted(extras, key=lambda entry: int(entry["dex_id"])):
                append_set([extra], "special", COMBAT[int(extra["combat_id"])],
                           "standalone-piece-sharing-model;generated-review-required")
    return set_rows, member_rows


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dex-dump", required=True, type=Path)
    parser.add_argument("--data", required=True, type=Path)
    parser.add_argument("--sets", required=True, type=Path)
    parser.add_argument("--members", required=True, type=Path)
    args = parser.parse_args()
    sets, members = build(args.dex_dump, args.data)
    write_csv(args.sets, ["set_id", "rank", "combat", "model_id", "name_cn", "name_en",
                               "display_order", "review_status", "source", "notes"], sets)
    write_csv(args.members, ["set_id", "part", "dex_id", "gender", "slot_order", "save_type",
                                  "save_id", "mapping_source", "notes"], members)
    print(f"sets={len(sets)} members={len(members)} unmapped={sum(not row['save_id'] for row in members)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
