#!/usr/bin/env python3
"""Validate the generated MH3G SQLite data layer and release boundaries."""

from __future__ import annotations

import argparse
import hashlib
import json
import sqlite3
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
EXPECTED_COUNTS = {
    "character_options": 47,
    "items": 1533,
    "equipment_types": 18,
    "weapons": 1437,
    "armors": 1924,
    "skill_trees": 126,
    "active_skills": 238,
    "armor_skill_points": 6814,
    "decorations": 230,
    "save_decorations": 196,
    "decoration_skill_points": 405,
    "charm_classes": 10,
    "charm_combinations": 121952,
}


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def require(condition: bool, message: str, errors: list[str]) -> None:
    if not condition:
        errors.append(message)


def validate(data_dir: Path) -> None:
    errors: list[str] = []
    database = data_dir / "mh3g.sqlite"
    manifest_path = data_dir / "manifest.json"
    readme = data_dir / "README.md"
    require(database.is_file(), "missing data/mh3g.sqlite", errors)
    require(manifest_path.is_file(), "missing data/manifest.json", errors)
    require(readme.is_file(), "missing data/README.md", errors)
    static_csv = sorted(path.relative_to(data_dir).as_posix() for path in data_dir.rglob("*.csv"))
    require(not static_csv, f"runtime static CSV files remain: {static_csv}", errors)
    if errors:
        raise SystemExit("data validation failed:\n- " + "\n- ".join(errors))

    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    require(manifest.get("format") == "mh3g-save-editor-data-manifest-v1", "manifest format mismatch", errors)
    require(manifest.get("database", {}).get("sha256") == sha256(database), "database SHA-256 mismatch", errors)
    require(manifest.get("database", {}).get("bytes") == database.stat().st_size, "database byte count mismatch", errors)
    for key, expected_name in (("save_ids", "mh3g_static_crosswalk.json"),
                               ("armor_native", "mh3g_armor_native_parameters.json")):
        entry = manifest.get("crosswalks", {}).get(key, {})
        path = ROOT / "tools" / expected_name
        require(entry.get("file") == expected_name, f"manifest crosswalk name mismatch for {key}", errors)
        require(path.is_file() and entry.get("sha256") == sha256(path), f"manifest crosswalk hash mismatch for {key}", errors)

    uri = f"file:{database.as_posix()}?mode=ro"
    connection = sqlite3.connect(uri, uri=True)
    try:
        require(connection.execute("PRAGMA user_version").fetchone()[0] == 1, "user_version must be 1", errors)
        require(connection.execute("PRAGMA integrity_check").fetchone()[0] == "ok", "integrity_check failed", errors)
        require(connection.execute("PRAGMA foreign_key_check").fetchone() is None, "foreign_key_check failed", errors)
        require(connection.execute("SELECT value FROM meta WHERE key='format'").fetchone()[0] == "mh3g-save-editor-data-v1", "database format mismatch", errors)
        for table, expected in EXPECTED_COUNTS.items():
            actual = connection.execute(f"SELECT count(*) FROM {table}").fetchone()[0]
            require(actual == expected, f"{table}: expected {expected}, got {actual}", errors)
            require(manifest.get("counts", {}).get(table) == actual, f"manifest count mismatch for {table}", errors)
            rows = connection.execute(f"SELECT * FROM {table} ORDER BY rowid").fetchall()
            payload = json.dumps(rows, ensure_ascii=False, separators=(",", ":"), default=str).encode()
            logical_hash = hashlib.sha256(payload).hexdigest()
            require(manifest.get("logical_hashes", {}).get(table) == logical_hash,
                    f"manifest logical hash mismatch for {table}", errors)

        require(connection.execute("SELECT count(*) FROM weapons WHERE mapping_status='confirmed'").fetchone()[0] == 1421,
                "all 1421 natural weapons must be mapped", errors)
        require(connection.execute("SELECT count(*) FROM armors WHERE mapping_status='confirmed'").fetchone()[0] == 1600,
                "exactly 1600 Dex armors must have confirmed save mappings", errors)
        require(connection.execute("SELECT count(*) FROM armors WHERE mapping_status='confirmed_mh3g'").fetchone()[0] == 52,
                "exactly 52 additional save-local armors must have MH3G native parameters", errors)
        require(connection.execute("SELECT count(*) FROM armors WHERE mapping_status='confirmed_mh3g' AND "
                                   "base_defense IS NOT NULL AND slots IS NOT NULL AND combat IS NOT NULL AND gender IS NOT NULL").fetchone()[0] == 52,
                "MH3G-native armor parameter coverage is incomplete", errors)
        require(connection.execute("SELECT count(*) FROM armors WHERE dex_id IS NOT NULL").fetchone()[0] == 1651,
                "all 1651 Dex armors must be present", errors)
        require(connection.execute("SELECT count(DISTINCT save_type || ':' || save_id) FROM weapons WHERE save_id IS NOT NULL").fetchone()[0] == EXPECTED_COUNTS["weapons"],
                "weapon save keys are not unique", errors)
        require(connection.execute("SELECT count(DISTINCT save_type || ':' || save_id) FROM armors WHERE save_id IS NOT NULL").fetchone()[0] == 1873,
                "armor save arrays are not dense and unique", errors)
        require(connection.execute("SELECT count(*) FROM charm_combinations WHERE skill1_points < 0 OR skill2_points < 0").fetchone()[0] > 0,
                "negative charm skills were lost", errors)
        require(connection.execute("SELECT count(*) FROM charm_combinations WHERE slots=3").fetchone()[0] > 0,
                "three-slot charms were lost", errors)
        require(connection.execute("SELECT count(*) FROM decorations WHERE mapping_status='unknown'").fetchone()[0] > 0,
                "ambiguous decoration variants must remain unknown", errors)
        require(connection.execute("SELECT count(*) FROM sources WHERE name='ID_res.arc' AND sha256='81e316bdfb0e65c1b05f1b375265aaff2cc3a1dc4c8cb5b8be23f0abd9b73087'").fetchone()[0] == 1,
                "ID_res authority hash mismatch", errors)
        require(connection.execute("SELECT count(*) FROM sources WHERE name='MH3G ExeFS .code' AND sha256='5374eaac8de5395f346933c4523019a6f643b72e3a73778ccf9a2ac4c32aaa1d'").fetchone()[0] == 1,
                "ExeFS native-table source hash mismatch", errors)
        require(connection.execute("SELECT count(*) FROM sources WHERE name='mh3g_armor_native_parameters.json'").fetchone()[0] == 1,
                "native armor crosswalk source is missing", errors)
        require(connection.execute("SELECT min(save_id),max(save_id),count(*) FROM items").fetchone() == (0, 1532, 1533),
                "item save-ID array is not dense 0..1532", errors)
        armor_ranges = {1: 382, 2: 363, 3: 371, 4: 377, 5: 380}
        weapon_ranges = {7: 136, 8: 141, 9: 135, 10: 146, 11: 93, 13: 97,
                         14: 115, 15: 114, 16: 115, 17: 117, 18: 126, 19: 102}
        for save_type, count in armor_ranges.items():
            actual = connection.execute("SELECT min(save_id),max(save_id),count(*) FROM armors WHERE save_type=?", (save_type,)).fetchone()
            require(actual == (0, count - 1, count), f"armor type {save_type} is not dense 0..{count - 1}", errors)
        for save_type, count in weapon_ranges.items():
            actual = connection.execute("SELECT min(save_id),max(save_id),count(*) FROM weapons WHERE save_type=?", (save_type,)).fetchone()
            require(actual == (0, count - 1, count), f"weapon type {save_type} is not dense 0..{count - 1}", errors)
    finally:
        connection.close()

    forbidden = {".arc", ".mod", ".tex", ".mrl", ".cci"}
    bundled = sorted(path.relative_to(ROOT).as_posix() for path in ROOT.rglob("*") if path.is_file() and path.suffix.lower() in forbidden)
    require(not bundled, f"game/model resources are bundled: {bundled}", errors)
    if errors:
        raise SystemExit("data validation failed:\n- " + "\n- ".join(errors))
    print("validated mh3g.sqlite: integrity, foreign keys, mappings, signed charms and release boundaries passed")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("data", nargs="?", type=Path, default=ROOT / "data")
    args = parser.parse_args()
    validate(args.data.resolve())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
