#!/usr/bin/env python3
"""Validate generated MH3G/MH3U data and its game-resource provenance."""

from __future__ import annotations

import argparse
import csv
import hashlib
import io
import json
import struct
from pathlib import Path


BASE_COLUMNS = ["id", "name", "english", "source"]
ROOT = Path(__file__).resolve().parents[1]
MAPPING_PATH = Path(__file__).with_name("data_mapping.json")
CROSSWALK_PATH = Path(__file__).with_name("mh3g_display_crosswalk.json")
GENERATOR_VERSION = "2.0.0"
SAVE_SIZE = 0x8A00
WIIU_HEADER_SIZE = 0x24
ITEM_SECTIONS = ((0x00AC, 24), (0x010C, 32), (0x018C, 1000))
BOX_OFFSET = 0x112C
BOX_SLOTS = 1000
BOX_RECORD_SIZE = 16
EQUIPMENT_TABLES = {
    1: "chest_armors", 2: "arms_armors", 3: "waist_armors",
    4: "legs_armors", 5: "head_armors",
    7: "gs_weapons", 8: "sns_weapons", 9: "h_weapons",
    10: "l_weapons", 11: "hbg_weapons", 13: "lbg_weapons",
    14: "ls_weapons", 15: "sa_weapons", 16: "gl_weapons",
    17: "bow_weapons", 18: "db_weapons", 19: "hh_weapons",
}


class Errors:
    def __init__(self) -> None:
        self.messages: list[str] = []

    def require(self, condition: bool, message: str) -> None:
        if not condition:
            self.messages.append(message)


def read_json(path: Path) -> dict:
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def load_csv(path: Path, errors: Errors) -> tuple[list[str], list[dict[str, str]]]:
    raw = path.read_bytes()
    errors.require(not raw.startswith(b"\xef\xbb\xbf"), f"{path}: UTF-8 BOM is not allowed")
    errors.require(b"\r" not in raw, f"{path}: only LF line endings are allowed")
    try:
        text = raw.decode("utf-8", errors="strict")
    except UnicodeDecodeError as exc:
        errors.messages.append(f"{path}: invalid UTF-8: {exc}")
        return [], []
    reader = csv.DictReader(io.StringIO(text, newline=""))
    try:
        rows = list(reader)
    except csv.Error as exc:
        errors.messages.append(f"{path}: invalid CSV: {exc}")
        return list(reader.fieldnames or []), []
    return list(reader.fieldnames or []), rows


def parse_ids(relative: str, rows: list[dict[str, str]], errors: Errors) -> list[int]:
    identifiers: list[int] = []
    for line, row in enumerate(rows, 2):
        raw_id = row.get("id", "")
        if not raw_id.isdecimal():
            errors.messages.append(f"{relative}:{line}: ID must be unsigned decimal, got {raw_id!r}")
            continue
        identifier = int(raw_id, 10)
        identifiers.append(identifier)
        for column in ("name", "english", "source"):
            errors.require(bool(row.get(column, "").strip()), f"{relative}:{line}: {column} must not be empty")
    errors.require(len(identifiers) == len(set(identifiers)), f"{relative}: duplicate IDs")
    errors.require(identifiers == sorted(identifiers), f"{relative}: IDs are not sorted")
    return identifiers


def validate_sample(path: Path, table_counts: dict[str, int], errors: Errors) -> None:
    raw = path.read_bytes()
    if len(raw) == SAVE_SIZE:
        base = 0
        endian = "<"
    elif len(raw) == SAVE_SIZE + WIIU_HEADER_SIZE:
        base = WIIU_HEADER_SIZE
        endian = ">"
        errors.require(raw[0x1C:0x20] == b"\x00\x00\x8a\x00", f"{path}: invalid Wii U header")
    else:
        errors.messages.append(
            f"{path}: expected {SAVE_SIZE} byte 3DS or {SAVE_SIZE + WIIU_HEADER_SIZE} byte Wii U save"
        )
        return

    item_count = table_counts["items"]
    for section_offset, slots in ITEM_SECTIONS:
        for slot in range(slots):
            item_id, quantity = struct.unpack_from(endian + "HH", raw, base + section_offset + slot * 4)
            if quantity == 0 and item_id == 0:
                continue
            errors.require(item_id < item_count, f"{path}: item ID {item_id} outside game array")

    for slot in range(BOX_SLOTS):
        offset = base + BOX_OFFSET + slot * BOX_RECORD_SIZE
        equipment_type = raw[offset]
        if equipment_type == 0:
            continue
        equipment_id = struct.unpack_from(endian + "H", raw, offset + 2)[0]
        if equipment_type == 6:
            continue
        table = EQUIPMENT_TABLES.get(equipment_type)
        errors.require(table is not None, f"{path}: equipment slot {slot} has unknown type {equipment_type}")
        if table is not None:
            errors.require(
                equipment_id < table_counts[table],
                f"{path}: equipment slot {slot} type {equipment_type} ID {equipment_id} outside {table}",
            )


def validate(data_dir: Path, game_names_path: Path | None, samples: list[Path]) -> None:
    errors = Errors()
    mapping = read_json(MAPPING_PATH)
    crosswalk = read_json(CROSSWALK_PATH)
    manifest_path = data_dir / "manifest.json"
    manifest = read_json(manifest_path)
    errors.require(manifest.get("format") == "mh3g-save-editor-data-v1", "manifest: invalid format")
    errors.require(manifest.get("generator_version") == GENERATOR_VERSION, "manifest: generator version mismatch")
    errors.require(manifest.get("font_remap_warning") is True, "manifest: font-remap warning must be preserved")

    resource = mapping["game_resource"]
    for key, expected in resource.items():
        errors.require(manifest.get("game_resource", {}).get(key) == expected, f"manifest: game resource {key} mismatch")

    csv_paths = sorted(path for path in data_dir.rglob("*.csv") if path.is_file())
    actual_files = {path.relative_to(data_dir).as_posix() for path in csv_paths}
    manifest_files = set(manifest.get("files", {}))
    errors.require(actual_files == manifest_files, "manifest: CSV file set mismatch")

    loaded: dict[str, tuple[list[int], list[dict[str, str]]]] = {}
    for path in csv_paths:
        relative = path.relative_to(data_dir).as_posix()
        columns, rows = load_csv(path, errors)
        errors.require(columns == BASE_COLUMNS, f"{relative}: columns must be {BASE_COLUMNS}, got {columns}")
        identifiers = parse_ids(relative, rows, errors)
        loaded[relative] = (identifiers, rows)
        entry = manifest.get("files", {}).get(relative, {})
        errors.require(entry.get("rows") == len(rows), f"manifest: {relative} row count mismatch")
        errors.require(entry.get("sha256") == sha256(path), f"manifest: {relative} SHA-256 mismatch")

    for table, count in mapping["tables"].items():
        cn_relative = f"cn/{table}.csv"
        en_relative = f"en/{table}.csv"
        errors.require(cn_relative in loaded and en_relative in loaded, f"{table}: missing localized CSV")
        if cn_relative not in loaded or en_relative not in loaded:
            continue
        cn_ids, cn_rows = loaded[cn_relative]
        en_ids, en_rows = loaded[en_relative]
        expected_ids = list(range(count))
        errors.require(cn_ids == expected_ids, f"{cn_relative}: must cover every game ID 0..{count - 1}")
        errors.require(en_ids == expected_ids, f"{en_relative}: must cover every game ID 0..{count - 1}")
        errors.require(cn_ids == en_ids, f"{table}: Chinese and English ID sets differ")
        for index, (cn_row, en_row) in enumerate(zip(cn_rows, en_rows)):
            errors.require(cn_row["english"] == en_row["english"], f"{table} ID {index}: English columns differ")
            errors.require(en_row["name"] == en_row["english"], f"{en_relative} ID {index}: name must equal english")
            errors.require(cn_row["source"] == en_row["source"], f"{table} ID {index}: source differs by language")
        table_manifest = manifest.get("authoritative_tables", {}).get(table, {})
        errors.require(table_manifest.get("rows") == count, f"manifest: {table} authoritative count mismatch")

    for table, entries in crosswalk.get("tables", {}).items():
        count = mapping["tables"].get(table)
        errors.require(count is not None, f"crosswalk: unknown table {table}")
        if count is None:
            continue
        for raw_id, names in entries.items():
            identifier = int(raw_id, 10)
            errors.require(0 <= identifier < count, f"crosswalk: {table} ID {identifier} is outside game array")
            errors.require(isinstance(names, list) and len(names) == 2, f"crosswalk: {table} ID {identifier} names invalid")
            if f"cn/{table}.csv" in loaded and 0 <= identifier < len(loaded[f"cn/{table}.csv"][1]):
                row = loaded[f"cn/{table}.csv"][1][identifier]
                errors.require(row["name"] == names[0], f"crosswalk: {table} ID {identifier} Chinese name mismatch")
                errors.require(row["english"] == names[1], f"crosswalk: {table} ID {identifier} English name mismatch")

    if game_names_path is not None:
        game = read_json(game_names_path)
        errors.require(game.get("format") == mapping["game_export_format"], "game export: format mismatch")
        errors.require(game.get("font_remap_warning") is True, "game export: expected cn-font-remap warning")
        for key, expected in resource.items():
            errors.require(game.get("source", {}).get(key) == expected, f"game export: source {key} mismatch")
        errors.require(set(game.get("tables", {})) == set(mapping["tables"]), "game export: table set mismatch")
        for table, count in mapping["tables"].items():
            strings = game.get("tables", {}).get(table, [])
            errors.require(len(strings) == count, f"game export: {table} expected {count} strings, got {len(strings)}")
            name_resource = game.get("resources", {}).get(table)
            description_resource = game.get("paired_resources", {}).get(table)
            errors.require(
                manifest.get("authoritative_tables", {}).get(table, {}).get("name_resource") == name_resource,
                f"manifest: {table} name-resource metadata mismatch",
            )
            errors.require(
                manifest.get("authoritative_tables", {}).get(table, {}).get("description_resource") == description_resource,
                f"manifest: {table} description-resource metadata mismatch",
            )

    for sample in samples:
        validate_sample(sample, mapping["tables"], errors)

    if errors.messages:
        raise SystemExit("data validation failed:\n- " + "\n- ".join(errors.messages))
    print(
        f"validated {len(csv_paths)} CSV files; 18 game-array tables cover all authoritative IDs; "
        f"{len(samples)} save sample(s) checked"
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("data", nargs="?", type=Path, default=ROOT / "data")
    parser.add_argument("--game-names", type=Path, help="optional JSON from export_game_names.py")
    parser.add_argument("--sample", action="append", default=[], type=Path, help="3DS or Wii U character file; repeatable")
    args = parser.parse_args()
    validate(
        args.data.resolve(),
        args.game_names.resolve() if args.game_names else None,
        [path.resolve() for path in args.sample],
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
