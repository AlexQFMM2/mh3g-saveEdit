#!/usr/bin/env python3
"""Validate the generated MH3G weapon encyclopedia."""

from __future__ import annotations

import argparse
import hashlib
import json
import sqlite3
from pathlib import Path


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def scalar(db: sqlite3.Connection, sql: str) -> int:
    return int(db.execute(sql).fetchone()[0])


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("data", nargs="?", type=Path, default=Path(__file__).resolve().parents[1] / "data")
    args = parser.parse_args()
    database = args.data / "encyclopedia.sqlite"
    manifest_path = args.data / "encyclopedia-manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if manifest.get("format") != "mh3g-encyclopedia-manifest-v3":
        raise ValueError("unsupported encyclopedia manifest")
    if sha256(database) != manifest["database"]["sha256"]:
        raise ValueError("encyclopedia.sqlite hash does not match manifest")
    db = sqlite3.connect(f"file:{database}?mode=ro", uri=True)
    try:
        if db.execute("PRAGMA integrity_check").fetchone()[0] != "ok":
            raise ValueError("SQLite integrity check failed")
        expected = manifest["counts"]
        checks = {
            "weapon_types": scalar(db, "SELECT count(*) FROM weapon_types"),
            "weapons": scalar(db, "SELECT count(*) FROM weapons"),
            "mapped_weapons": scalar(db, "SELECT count(*) FROM weapons WHERE writable=1"),
            "items": scalar(db, "SELECT count(*) FROM items"),
            "mapped_items": scalar(db, "SELECT count(*) FROM items WHERE writable=1"),
            "materials": scalar(db, "SELECT count(*) FROM weapon_materials"),
            "model_resources": scalar(db, "SELECT count(*) FROM model_resources"),
            "weapon_models": scalar(db, "SELECT count(*) FROM weapon_models"),
            "armors": scalar(db, "SELECT count(*) FROM armors"),
            "mapped_armors": scalar(db, "SELECT count(*) FROM armors WHERE writable=1"),
            "armor_sets": scalar(db, "SELECT count(*) FROM armor_sets"),
            "armor_set_members": scalar(db, "SELECT count(*) FROM armor_set_members"),
            "armor_materials": scalar(db, "SELECT count(*) FROM armor_materials"),
            "skill_trees": scalar(db, "SELECT count(*) FROM skill_trees"),
            "active_skills": scalar(db, "SELECT count(*) FROM active_skills"),
            "armor_skill_points": scalar(db, "SELECT count(*) FROM armor_skill_points"),
            "armor_model_resources": scalar(db, "SELECT count(*) FROM armor_model_resources"),
            "armor_models_confirmed_exefs": scalar(db, "SELECT count(*) FROM armors WHERE model_mapping_status='confirmed_exefs'"),
            "armor_models_shared_appearance": scalar(db, "SELECT count(*) FROM armors WHERE model_mapping_status='exact_shared_appearance'"),
            "armor_models_unmapped": scalar(db, "SELECT count(*) FROM armors WHERE model_mapping_status='unmapped'"),
            "character_model_resources": scalar(db, "SELECT count(*) FROM character_model_resources"),
        }
        if checks != expected:
            raise ValueError(f"row counts differ: expected {expected}, got {checks}")
        if checks["weapon_types"] != 12 or checks["weapons"] != 1421:
            raise ValueError("unexpected MH3G weapon coverage")
        if checks["model_resources"] != 558 or checks["weapon_models"] != checks["mapped_weapons"]:
            raise ValueError("unexpected MH3G weapon model coverage")
        if checks["armors"] != 1651 or checks["mapped_armors"] != 1600 or checks["armor_sets"] != 331:
            raise ValueError("unexpected MH3G armor coverage")
        if checks["armor_set_members"] != checks["armors"] or checks["armor_materials"] != 6398:
            raise ValueError("unexpected MH3G armor relationship coverage")
        if checks["skill_trees"] != 123 or checks["active_skills"] != 238 or checks["armor_skill_points"] != 6814:
            raise ValueError("unexpected MH3G armor skill coverage")
        if checks["armor_model_resources"] != 2004:
            raise ValueError("unexpected MH3G armor model inventory")
        if scalar(db, "PRAGMA user_version") != 3:
            raise ValueError("encyclopedia database user_version is not 3")
        if (checks["armor_models_confirmed_exefs"] != 1600
                or checks["armor_models_shared_appearance"] != 31
                or checks["armor_models_unmapped"] != 20
                or checks["character_model_resources"] != 50):
            raise ValueError("unexpected armor/character model mapping coverage")
        if scalar(db, "SELECT count(*) FROM weapons WHERE name_cn='' OR name_en='' OR name_jp=''"):
            raise ValueError("weapon with empty localized name")
        if scalar(db, "SELECT count(*) FROM weapons WHERE min(sharp_red,sharp_orange,sharp_yellow,sharp_green,sharp_blue,sharp_white,sharp_purple)<0"):
            raise ValueError("negative sharpness segment")
        if scalar(db, "SELECT count(*) FROM weapon_edges e LEFT JOIN weapons p ON p.dex_id=e.parent_dex_id LEFT JOIN weapons c ON c.dex_id=e.child_dex_id WHERE p.dex_id IS NULL OR c.dex_id IS NULL"):
            raise ValueError("dangling weapon edge")
        if scalar(db, "SELECT count(*) FROM weapon_materials m LEFT JOIN items i ON i.dex_id=m.item_dex_id WHERE i.dex_id IS NULL"):
            raise ValueError("dangling material item")
        if scalar(db, "SELECT count(*) FROM weapon_models m LEFT JOIN weapons w ON w.dex_id=m.weapon_dex_id LEFT JOIN model_resources r ON r.model_key=m.model_key WHERE w.dex_id IS NULL OR r.model_key IS NULL"):
            raise ValueError("dangling weapon model mapping")
        if scalar(db, "SELECT count(*) FROM armors WHERE name_cn='' OR name_en='' OR name_jp='' OR slots NOT BETWEEN 0 AND 3"):
            raise ValueError("armor with invalid names or slots")
        if scalar(db, "SELECT count(*) FROM armor_set_members m LEFT JOIN armor_sets s ON s.set_id=m.set_id LEFT JOIN armors a ON a.dex_id=m.armor_dex_id WHERE s.set_id IS NULL OR a.dex_id IS NULL"):
            raise ValueError("dangling armor set member")
        if scalar(db, "SELECT count(*) FROM armor_materials m LEFT JOIN armors a ON a.dex_id=m.armor_dex_id LEFT JOIN items i ON i.dex_id=m.item_dex_id WHERE a.dex_id IS NULL OR i.dex_id IS NULL"):
            raise ValueError("dangling armor material")
        if scalar(db, "SELECT count(*) FROM armor_skill_points p LEFT JOIN armors a ON a.dex_id=p.armor_dex_id LEFT JOIN skill_trees s ON s.id=p.skill_tree_id WHERE a.dex_id IS NULL OR s.id IS NULL"):
            raise ValueError("dangling armor skill points")
        if scalar(db, "SELECT count(*) FROM active_skills a LEFT JOIN skill_trees s ON s.id=a.skill_tree_id WHERE s.id IS NULL"):
            raise ValueError("dangling active skill")
        if scalar(db, "SELECT count(*) FROM armors WHERE model_mapping_status='unmapped' AND (male_model_id IS NOT NULL OR female_model_id IS NOT NULL)"):
            raise ValueError("unmapped armor unexpectedly has a model ID")
        if scalar(db, "SELECT count(*) FROM armors a WHERE a.male_model_id>0 AND NOT EXISTS (SELECT 1 FROM armor_model_resources r WHERE r.model_id=a.male_model_id AND r.gender='male' AND r.part=a.part)"):
            raise ValueError("male armor model references a missing resource")
        if scalar(db, "SELECT count(*) FROM armors a WHERE a.female_model_id>0 AND NOT EXISTS (SELECT 1 FROM armor_model_resources r WHERE r.model_id=a.female_model_id AND r.gender='female' AND r.part=a.part)"):
            raise ValueError("female armor model references a missing resource")
        expected_samples = {
            1: (1, 1),       # Leather / pl001
            14: (2, 2),      # Chainmail / pl002
            72: (3, 3),      # Jaggi / pl003
        }
        for dex_id, models in expected_samples.items():
            actual = db.execute("SELECT male_model_id,female_model_id FROM armors WHERE dex_id=?", (dex_id,)).fetchone()
            if actual != models:
                raise ValueError(f"armor {dex_id}: expected models {models}, got {actual}")
    finally:
        db.close()
    print(json.dumps(checks, ensure_ascii=False, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
