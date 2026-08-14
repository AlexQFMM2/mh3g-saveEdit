#!/usr/bin/env python3
"""Build the deterministic MH3G/MH3U save-editor dataset.

Game resource arrays determine every item, weapon, and armor ID.  Existing
MH3U display lists supply names for entries shared with the western release;
an explicit reviewed crosswalk supplies Japan-only entries.  No row number in
a filtered third-party list is ever treated as a save ID.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import shutil
import tempfile
from pathlib import Path


GENERATOR_VERSION = "2.0.0"
BASE_COLUMNS = ("id", "name", "english", "source")
GAME_ARRAY_SOURCE = "mh3g-id-res-game-array"
LEGACY_SOURCE = GAME_ARRAY_SOURCE + "+mh3u-legacy-display-list"
CROSSWALK_SOURCE = GAME_ARRAY_SOURCE + "+reviewed-japan-only-crosswalk"
PLACEHOLDER_SOURCE = GAME_ARRAY_SOURCE + "+placeholder"
OTHER_DATA_SOURCE = "mh3u-legacy-display-list"

ROOT = Path(__file__).resolve().parents[1]
MAPPING_PATH = Path(__file__).with_name("data_mapping.json")
CROSSWALK_PATH = Path(__file__).with_name("mh3g_display_crosswalk.json")


def read_json(path: Path) -> dict:
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def read_csv(path: Path) -> list[dict[str, str]]:
    with path.open("r", encoding="utf-8-sig", newline="") as handle:
        reader = csv.DictReader(handle)
        if tuple(reader.fieldnames or ()) != BASE_COLUMNS:
            raise ValueError(f"{path}: expected columns {BASE_COLUMNS}, got {reader.fieldnames}")
        return list(reader)


def write_csv(path: Path, rows: list[dict[str, object]]) -> int:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=BASE_COLUMNS, lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)
    return len(rows)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def is_placeholder(value: str) -> bool:
    folded = value.strip().casefold()
    return "ダミー" in value or folded == "dummy" or folded.startswith("dummy ")


def load_reference(reference_data: Path, table: str) -> dict[int, tuple[str, str]]:
    cn_rows = read_csv(reference_data / "cn" / f"{table}.csv")
    en_rows = read_csv(reference_data / "en" / f"{table}.csv")
    cn = {int(row["id"], 10): row for row in cn_rows}
    en = {int(row["id"], 10): row for row in en_rows}
    if len(cn) != len(cn_rows) or len(en) != len(en_rows):
        raise ValueError(f"{table}: duplicate IDs in reference data")
    if set(cn) != set(en):
        raise ValueError(f"{table}: Chinese and English reference ID sets differ")
    result: dict[int, tuple[str, str]] = {}
    for identifier in sorted(cn):
        english = cn[identifier]["english"].strip() or en[identifier]["name"].strip()
        if english != en[identifier]["english"].strip():
            raise ValueError(f"{table} ID {identifier}: English columns differ by language")
        result[identifier] = (cn[identifier]["name"].strip(), english)
    return result


def special_empty(table: str, identifier: int) -> tuple[str, str] | None:
    if table == "items":
        if identifier == 0:
            return "未使用槽 0", "Unused Slot 0"
        if identifier == 1:
            return "没有道具", "No Item"
        return None
    if identifier == 0:
        return "无装备", "None"
    return None


def make_authoritative_table(
    table: str,
    game_strings: list[str],
    reference: dict[int, tuple[str, str]],
    crosswalk: dict[int, tuple[str, str]],
) -> tuple[list[dict[str, object]], list[dict[str, object]]]:
    invalid_crosswalk_ids = set(crosswalk) - set(range(len(game_strings)))
    if invalid_crosswalk_ids:
        raise ValueError(f"{table}: crosswalk IDs outside game array: {sorted(invalid_crosswalk_ids)}")
    invalid_crosswalk_slots = [
        identifier for identifier in crosswalk
        if special_empty(table, identifier) is not None or is_placeholder(game_strings[identifier])
    ]
    if invalid_crosswalk_slots:
        raise ValueError(f"{table}: crosswalk targets placeholder slots: {sorted(invalid_crosswalk_slots)}")
    rows_cn: list[dict[str, object]] = []
    rows_en: list[dict[str, object]] = []
    for identifier, game_string in enumerate(game_strings):
        empty = special_empty(table, identifier)
        if empty is not None:
            cn_name, english = empty
            source = PLACEHOLDER_SOURCE
        elif is_placeholder(game_string):
            cn_name = english = f"DUMMY {identifier}"
            source = PLACEHOLDER_SOURCE
        elif identifier in crosswalk:
            cn_name, english = crosswalk[identifier]
            source = CROSSWALK_SOURCE
        elif identifier in reference:
            cn_name, english = reference[identifier]
            source = LEGACY_SOURCE
        else:
            raise ValueError(
                f"{table} ID {identifier} ({game_string!r}) is a valid game-array slot "
                "without an explicit display-name mapping"
            )
        if not cn_name or not english:
            raise ValueError(f"{table} ID {identifier}: empty display name")
        common = {"id": identifier, "english": english, "source": source}
        rows_cn.append({"id": identifier, "name": cn_name, "english": english, "source": source})
        rows_en.append({**common, "name": english})
    return rows_cn, rows_en


def make_other_table(reference_data: Path, relative: Path, language: str) -> list[dict[str, object]]:
    rows = read_csv(reference_data / language / relative)
    result: list[dict[str, object]] = []
    identifiers: set[int] = set()
    for row in rows:
        identifier = int(row["id"], 10)
        if identifier in identifiers:
            raise ValueError(f"{relative}: duplicate ID {identifier}")
        identifiers.add(identifier)
        name = row["name"].strip()
        english = row["english"].strip()
        if not name or not english:
            raise ValueError(f"{language}/{relative}: ID {identifier} has an empty name")
        result.append({
            "id": identifier,
            "name": name,
            "english": english,
            "source": row["source"].strip() or OTHER_DATA_SOURCE,
        })
    return sorted(result, key=lambda row: int(row["id"]))


def build(game_names_path: Path, reference_data: Path, output: Path) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    mapping = read_json(MAPPING_PATH)
    crosswalk_raw = read_json(CROSSWALK_PATH)
    game_export = read_json(game_names_path)
    if game_export.get("format") != mapping["game_export_format"]:
        raise ValueError(f"unsupported game export format {game_export.get('format')!r}")
    if not game_export.get("font_remap_warning"):
        raise ValueError("expected the audited cn-font-remap MH3G resource export")
    for key, expected in mapping["game_resource"].items():
        actual = game_export["source"].get(key)
        if actual != expected:
            raise ValueError(f"game resource {key} mismatch: expected {expected!r}, got {actual!r}")

    expected_tables = mapping["tables"]
    if set(game_export["tables"]) != set(expected_tables):
        raise ValueError("game export table set does not match data_mapping.json")
    if set(crosswalk_raw["tables"]) - set(expected_tables):
        raise ValueError("crosswalk contains an unknown game table")

    crosswalk: dict[str, dict[int, tuple[str, str]]] = {}
    for table in expected_tables:
        values: dict[int, tuple[str, str]] = {}
        for raw_id, names in crosswalk_raw["tables"].get(table, {}).items():
            identifier = int(raw_id, 10)
            if not isinstance(names, list) or len(names) != 2 or not all(isinstance(v, str) and v.strip() for v in names):
                raise ValueError(f"{table} crosswalk ID {identifier}: expected [Chinese, English]")
            values[identifier] = (names[0].strip(), names[1].strip())
        crosswalk[table] = values

    with tempfile.TemporaryDirectory(prefix="mh3g-data-build-", dir=output.parent) as directory:
        stage = Path(directory) / "data"
        generated: dict[str, dict[str, object]] = {}
        for table, expected_count in expected_tables.items():
            game_strings = game_export["tables"][table]
            if len(game_strings) != expected_count:
                raise ValueError(f"{table}: expected {expected_count} game slots, got {len(game_strings)}")
            reference = load_reference(reference_data, table)
            rows_cn, rows_en = make_authoritative_table(table, game_strings, reference, crosswalk[table])
            for language, rows in (("cn", rows_cn), ("en", rows_en)):
                relative = Path(language) / f"{table}.csv"
                write_csv(stage / relative, rows)

        authoritative_files = {f"{table}.csv" for table in expected_tables}
        cn_files = {path.name for path in (reference_data / "cn").glob("*.csv")}
        en_files = {path.name for path in (reference_data / "en").glob("*.csv")}
        if cn_files != en_files:
            raise ValueError("Chinese and English reference file sets differ")
        for filename in sorted(cn_files - authoritative_files):
            for language in ("cn", "en"):
                relative = Path(filename)
                rows = make_other_table(reference_data, relative, language)
                write_csv(stage / language / relative, rows)

        reference_readme = reference_data / "README.md"
        if reference_readme.is_file():
            shutil.copy2(reference_readme, stage / "README.md")

        for path in sorted(stage.rglob("*.csv")):
            relative = path.relative_to(stage).as_posix()
            with path.open("r", encoding="utf-8", newline="") as handle:
                row_count = sum(1 for _ in csv.DictReader(handle))
            generated[relative] = {
                "rows": row_count,
                "sha256": sha256(path),
            }

        manifest = {
            "format": "mh3g-save-editor-data-v1",
            "generator_version": GENERATOR_VERSION,
            "game_resource": game_export["source"],
            "game_export_format": game_export["format"],
            "game_export_language": game_export["language"],
            "font_remap_warning": True,
            "authoritative_tables": {
                table: {
                    "rows": count,
                    "name_resource": game_export["resources"][table],
                    "description_resource": game_export["paired_resources"][table],
                }
                for table, count in sorted(expected_tables.items())
            },
            "files": generated,
        }
        (stage / "manifest.json").write_text(
            json.dumps(manifest, ensure_ascii=False, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
            newline="\n",
        )

        if output.exists():
            shutil.rmtree(output)
        shutil.copytree(stage, output)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game-names", required=True, type=Path, help="JSON from export_game_names.py")
    parser.add_argument("--reference-data", type=Path, default=ROOT / "data", help="existing reviewed display CSV directory")
    parser.add_argument("--output", type=Path, default=ROOT / "data", help="generated data directory")
    args = parser.parse_args()
    game_names = args.game_names.resolve()
    reference = args.reference_data.resolve()
    output = args.output.resolve()
    if reference == output:
        # Keep the reference alive while the atomically staged build replaces data/.
        with tempfile.TemporaryDirectory(prefix="mh3g-data-reference-", dir=output.parent) as directory:
            snapshot = Path(directory) / "data"
            shutil.copytree(reference, snapshot)
            build(game_names, snapshot, output)
    else:
        build(game_names, reference, output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
