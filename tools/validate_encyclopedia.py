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
        }
        if checks != expected:
            raise ValueError(f"row counts differ: expected {expected}, got {checks}")
        if checks["weapon_types"] != 12 or checks["weapons"] != 1421:
            raise ValueError("unexpected MH3G weapon coverage")
        if checks["model_resources"] != 558 or checks["weapon_models"] != checks["mapped_weapons"]:
            raise ValueError("unexpected MH3G weapon model coverage")
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
    finally:
        db.close()
    print(json.dumps(checks, ensure_ascii=False, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
