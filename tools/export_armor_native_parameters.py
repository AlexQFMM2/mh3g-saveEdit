#!/usr/bin/env python3
"""Export audited MH3G armor parameters from the five native ExeFS tables.

The executable is an offline input and is never bundled with the editor.  The
generated JSON is the reviewed crosswalk consumed by build_sqlite_data.py.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
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
FLAGS = {
    15: (0, 0), 7: (1, 0), 11: (2, 0),
    5: (1, 1), 9: (2, 1), 6: (1, 2), 10: (2, 2),
    13: (0, 1), 14: (0, 2),
}


def read_csv(path: Path) -> list[dict[str, str]]:
    with path.open("r", encoding="utf-8-sig", newline="") as handle:
        return list(csv.DictReader(handle))


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def signed(value: int) -> int:
    return value if value < 128 else value - 256


def decode(save_type: int, save_id: int, raw: bytes) -> dict[str, object]:
    if len(raw) != RECORD_SIZE:
        raise ValueError(f"armor {save_type}:{save_id}: truncated native record")
    flag = raw[6]
    combat, gender = FLAGS.get(flag, (None, None))
    skills: dict[int, int] = defaultdict(int)
    for offset in range(14, 24, 2):
        skill_id, points = raw[offset], signed(raw[offset + 1])
        if skill_id and points:
            skills[skill_id] += points
    return {
        "save_type": save_type,
        "save_id": save_id,
        "base_defense": raw[0],
        # These three bytes are deliberately retained without invented names.
        "unknown_01": raw[1],
        "male_model_id": raw[2],
        "female_model_id": raw[3],
        "unknown_04": raw[4],
        "unknown_05": raw[5],
        "equipment_flags": flag,
        "combat": combat,
        "gender": gender,
        "rarity": raw[7] + 1,
        "fire_res": signed(raw[8]),
        "water_res": signed(raw[9]),
        "thunder_res": signed(raw[10]),
        "ice_res": signed(raw[11]),
        "dragon_res": signed(raw[12]),
        "slots": raw[13],
        "skills": [{"skill_tree_id": key, "points": value} for key, value in sorted(skills.items())],
        "raw_hex": raw.hex(),
    }


def export(code_path: Path, dex_dump: Path, static_crosswalk: Path) -> dict[str, object]:
    code = code_path.read_bytes()
    if digest(code) != EXPECTED_SHA256:
        raise ValueError("MH3G ExeFS .code SHA-256 does not match the audited build")
    crosswalk = json.loads(static_crosswalk.read_text(encoding="utf-8"))
    if crosswalk.get("format") != "mh3g-static-crosswalk-v1":
        raise ValueError("unsupported static crosswalk format")

    attributes = {int(row["Amr_ID"]): row for row in read_csv(dex_dump / "tables/021_u1715.csv")}
    dex_skills: dict[int, dict[int, int]] = defaultdict(dict)
    for row in read_csv(dex_dump / "direct_sql/DB_SklTreetoAmr.csv"):
        dex_skills[int(row["Amr_ID"])][int(row["SklTree_ID"])] = int(row["Pt"])
    mapped = {
        (int(row["save_type"]), int(row["save_id"])): int(row["dex_id"])
        for row in crosswalk["armor_dex_crosswalk"] if row["save_id"] is not None
    }
    if len(attributes) != 1651 or len(mapped) != 1600:
        raise ValueError("unexpected Dex armor or save crosswalk count")

    legacy_by_type = {
        1: crosswalk["legacy_tables"]["chest_armors"],
        2: crosswalk["legacy_tables"]["arms_armors"],
        3: crosswalk["legacy_tables"]["waist_armors"],
        4: crosswalk["legacy_tables"]["legs_armors"],
        5: crosswalk["legacy_tables"]["head_armors"],
    }
    records: list[dict[str, object]] = []
    for save_type, (address, count) in sorted(TABLES.items()):
        legacy = legacy_by_type[save_type]
        if len(legacy) != count or [int(row["id"]) for row in legacy] != list(range(count)):
            raise ValueError(f"save type {save_type}: ID_res array is not dense 0..{count - 1}")
        offset = address - LOAD_BASE
        for save_id in range(count):
            raw = code[offset + save_id * RECORD_SIZE:offset + (save_id + 1) * RECORD_SIZE]
            value = decode(save_type, save_id, raw)
            dex_id = mapped.get((save_type, save_id))
            if dex_id is not None:
                attr = attributes[dex_id]
                expected = {
                    "base_defense": int(attr["Def"]), "combat": int(attr["BorG"]),
                    "gender": int(attr["MorF"]), "rarity": int(attr["Rare"]),
                    "slots": int(attr["Slot"]), "fire_res": int(attr["Res_Fire"]),
                    "water_res": int(attr["Res_Water"]), "thunder_res": int(attr["Res_Thunder"]),
                    "ice_res": int(attr["Res_Ice"]), "dragon_res": int(attr["Res_Dragon"]),
                }
                for field, expected_value in expected.items():
                    if value[field] != expected_value:
                        raise ValueError(
                            f"armor {save_type}:{save_id} / Dex {dex_id}: native {field} "
                            f"{value[field]} != Dex {expected_value}"
                        )
                native_skills = {int(row["skill_tree_id"]): int(row["points"]) for row in value["skills"]}
                if native_skills != dex_skills[dex_id]:
                    raise ValueError(f"armor {save_type}:{save_id} / Dex {dex_id}: native skills differ from Dex")
                value["dex_id"] = dex_id
                value["evidence"] = "native_and_dex"
            else:
                value["dex_id"] = None
                value["evidence"] = "native_only" if any(raw) else "empty_native_record"
            records.append(value)

    if len(records) != 1873:
        raise ValueError(f"expected 1873 native armor records, got {len(records)}")
    return {
        "format": "mh3g-armor-native-parameters-v1",
        "source": {
            "name": "MH3G ExeFS .code",
            "sha256": EXPECTED_SHA256,
            "load_base": LOAD_BASE,
            "record_size": RECORD_SIZE,
            "tables": {str(key): {"address": address, "count": count}
                       for key, (address, count) in sorted(TABLES.items())},
        },
        "field_evidence": {
            "decoded": ["base_defense", "male_model_id", "female_model_id", "equipment_flags",
                        "combat", "gender", "rarity", "fire_res", "water_res", "thunder_res",
                        "ice_res", "dragon_res", "slots", "skills"],
            "unknown": ["unknown_01", "unknown_04", "unknown_05"],
            "max_upgrade_level": "not present in the decoded 24-byte record; remains unknown",
        },
        "records": records,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--code", required=True, type=Path)
    parser.add_argument("--dex-dump", required=True, type=Path)
    parser.add_argument("--static-crosswalk", type=Path, default=Path(__file__).with_name("mh3g_static_crosswalk.json"))
    parser.add_argument("--output", type=Path, default=Path(__file__).with_name("mh3g_armor_native_parameters.json"))
    args = parser.parse_args()
    result = export(args.code.resolve(), args.dex_dump.resolve(), args.static_crosswalk.resolve())
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8", newline="\n")
    print("exported 1873 native armor records; 1600 records cross-checked against Dex")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
