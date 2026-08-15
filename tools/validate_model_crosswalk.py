#!/usr/bin/env python3
"""Validate the audited MH3G save-ID to weapon-model crosswalk."""

from __future__ import annotations

import argparse
import json
import sqlite3
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--crosswalk", type=Path, default=ROOT / "tools" / "mh3g_weapon_model_crosswalk.json")
    parser.add_argument("--database", type=Path, default=ROOT / "data" / "encyclopedia.sqlite")
    parser.add_argument("--resources", type=Path, help="Optional unpacked arc/weapon/mod directory")
    args = parser.parse_args()

    crosswalk = json.loads(args.crosswalk.read_text(encoding="utf-8"))
    if crosswalk.get("format") != "mh3g-weapon-model-crosswalk-v1":
        raise ValueError("unsupported crosswalk format")
    model_by_type_save: dict[tuple[int, int], tuple[str, str]] = {}
    all_models: set[str] = set()
    for dex_type, values in sorted((int(key), value) for key, value in crosswalk["types"].items()):
        model_ids: list[int] = []
        for model_id, count in values["model_id_runs"]:
            if int(model_id) <= 0 or int(count) <= 0:
                raise ValueError(f"type {dex_type}: invalid RLE pair")
            model_ids.extend([int(model_id)] * int(count))
        if len(model_ids) != int(values["save_id_count"]):
            raise ValueError(f"type {dex_type}: RLE count mismatch")
        folder = values["resource_folder"]
        for save_id, model_id in enumerate(model_ids, 1):
            key = f"{folder}_{model_id:02d}"
            relative = f"{folder}/{key}.arc"
            model_by_type_save[(dex_type, save_id)] = (key, relative)
            all_models.add(relative)

    # The parameter arrays contain two unused save-ID slots which are not Dex
    # weapon entities.  Keep them in the dense mapping so later IDs cannot
    # shift, while requiring all 1,421 displayable weapons below.
    if len(model_by_type_save) != 1423 or len(all_models) != 558:
        raise ValueError(f"unexpected coverage: parameter_slots={len(model_by_type_save)} models={len(all_models)}")

    database = sqlite3.connect(f"file:{args.database}?mode=ro", uri=True)
    try:
        rows = database.execute(
            "SELECT w.dex_type,w.save_id,wm.model_key,mr.arc_relative_path "
            "FROM weapons w JOIN weapon_models wm ON wm.weapon_dex_id=w.dex_id "
            "JOIN model_resources mr ON mr.model_key=wm.model_key ORDER BY w.dex_type,w.save_id"
        ).fetchall()
    finally:
        database.close()
    if len(rows) != 1421:
        raise ValueError(f"database has {len(rows)} weapon model rows")
    for dex_type, save_id, model_key, relative in rows:
        if model_by_type_save[(dex_type, save_id)] != (model_key, relative):
            raise ValueError(f"database mismatch for type {dex_type} save ID {save_id}")

    if args.resources:
        missing = sorted(relative for relative in all_models if not (args.resources / relative).is_file())
        if missing:
            raise ValueError(f"missing {len(missing)} ARC files; first: {missing[0]}")
    print(json.dumps({"parameter_slots": 1423, "weapon_mappings": 1421, "unique_arc_models": 558}, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
