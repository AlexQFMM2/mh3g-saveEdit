#!/usr/bin/env python3
"""Build the MH3G editor's deterministic, read-only SQLite game database.

Save-local IDs always come from the audited ID_res arrays captured in
mh3g_static_crosswalk.json.  The MH3G Dex dump only contributes metadata and
relations; its primary keys are never treated as save IDs.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import re
import sqlite3
import tempfile
import unicodedata
from collections import defaultdict
from pathlib import Path


VERSION = "1.2.0"
FORMAT = "mh3g-save-editor-data-v1"
DB_VERSION = 1
EXEFS_SHA256 = "5374eaac8de5395f346933c4523019a6f643b72e3a73778ccf9a2ac4c32aaa1d"
ROOT = Path(__file__).resolve().parents[1]
CROSSWALK = Path(__file__).with_name("mh3g_static_crosswalk.json")
ARMOR_NATIVE = Path(__file__).with_name("mh3g_armor_native_parameters.json")

TYPE_MAP = {
    1: (7, "gs_weapons"), 2: (14, "ls_weapons"),
    3: (8, "sns_weapons"), 4: (18, "db_weapons"),
    5: (9, "h_weapons"), 6: (19, "hh_weapons"),
    7: (10, "l_weapons"), 8: (16, "gl_weapons"),
    9: (15, "sa_weapons"), 10: (13, "lbg_weapons"),
    11: (11, "hbg_weapons"), 12: (17, "bow_weapons"),
}
ARMOR_TABLES = {
    1: "chest_armors", 2: "arms_armors", 3: "waist_armors",
    4: "legs_armors", 5: "head_armors",
}
ARMOR_PART = {1: 5, 2: 1, 3: 2, 4: 3, 5: 4}


def read_csv(path: Path) -> list[dict[str, str]]:
    with path.open("r", encoding="utf-8-sig", newline="") as handle:
        return list(csv.DictReader(handle))


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def normalized(value: str) -> str:
    value = unicodedata.normalize("NFKC", value).casefold().replace("’", "'")
    return re.sub(r"[\s・·【】\[\]()（）,，:：\"“”'‘’._-]", "", value)


def placeholder(name: str, english: str) -> int:
    value = f"{name} {english}".upper()
    return int(not name.strip() or "DUMMY" in value or value.strip(" -") in {"", "NULL", "NONE"})


def exact_indexes(rows: list[dict[str, object]]) -> tuple[dict[str, list[int]], dict[str, list[int]]]:
    cn: dict[str, list[int]] = defaultdict(list)
    en: dict[str, list[int]] = defaultdict(list)
    for row in rows:
        identifier = int(row["id"])
        cn[normalized(str(row["name"]))].append(identifier)
        en[normalized(str(row["english"]))].append(identifier)
    return cn, en


def resolve_exact(
    rows: list[dict[str, object]], name_cn: str, name_en: str, override: int | None = None
) -> tuple[int | None, str]:
    cn, en = exact_indexes(rows)
    en_match = en.get(normalized(name_en), [])
    if len(en_match) == 1:
        return en_match[0], "exact-en"
    cn_match = cn.get(normalized(name_cn), [])
    if len(cn_match) == 1:
        return cn_match[0], "exact-cn"
    if override is not None:
        return override, "reviewed-crosswalk"
    return None, "unmapped"


def sharpness(value: str) -> tuple[int, ...]:
    if not re.fullmatch(r"\d{14}", value):
        raise ValueError(f"invalid sharpness value: {value!r}")
    return tuple(reversed([int(value[index:index + 2]) for index in range(0, 14, 2)]))


def required_inputs(dex: Path) -> dict[str, Path]:
    return {
        "dex_manifest": dex / "manifest.json",
        "weapon_names": dex / "tables/004_u1704.csv",
        "weapon_attributes": dex / "tables/018_u1712.csv",
        "armor_names": dex / "tables/022_u1716.csv",
        "armor_attributes": dex / "tables/021_u1715.csv",
        "skill_tree_names": dex / "tables/007_u1707.csv",
        "active_skills": dex / "tables/019_u1713.csv",
        "active_skill_names": dex / "tables/020_u1714.csv",
        "armor_skill_points": dex / "direct_sql/DB_SklTreetoAmr.csv",
        "decorations": dex / "direct_sql/DB_Jew.csv",
        "decoration_skill_points": dex / "direct_sql/DB_SklTreetoJew.csv",
        "charms": dex / "direct_sql/DB_Tls.csv",
        "item_names": dex / "direct_sql/items_id_zh_en.csv",
    }


SCHEMA = """
PRAGMA page_size=4096;
PRAGMA encoding='UTF-8';
PRAGMA journal_mode=OFF;
PRAGMA synchronous=OFF;
PRAGMA foreign_keys=ON;
PRAGMA user_version=1;
CREATE TABLE meta(key TEXT PRIMARY KEY, value TEXT NOT NULL);
CREATE TABLE sources(name TEXT PRIMARY KEY, sha256 TEXT NOT NULL, detail TEXT NOT NULL);
CREATE TABLE character_options(
  kind TEXT NOT NULL, id INTEGER NOT NULL, name_cn TEXT NOT NULL,
  name_en TEXT NOT NULL, source TEXT NOT NULL, PRIMARY KEY(kind,id)
);
CREATE TABLE items(
  save_id INTEGER PRIMARY KEY, name_cn TEXT NOT NULL, name_en TEXT NOT NULL,
  source TEXT NOT NULL, is_placeholder INTEGER NOT NULL CHECK(is_placeholder IN(0,1))
);
CREATE TABLE equipment_types(
  save_type INTEGER PRIMARY KEY, subtype TEXT NOT NULL,
  name_cn TEXT NOT NULL, name_en TEXT NOT NULL, source TEXT NOT NULL
);
CREATE TABLE weapons(
  row_id INTEGER PRIMARY KEY, save_type INTEGER, save_id INTEGER, dex_id INTEGER UNIQUE,
  name_cn TEXT NOT NULL, name_en TEXT NOT NULL, name_jp TEXT NOT NULL DEFAULT '',
  is_placeholder INTEGER NOT NULL, mapping_status TEXT NOT NULL, mapping_source TEXT NOT NULL,
  rarity INTEGER, attack INTEGER, attribute1_id INTEGER, attribute1_value INTEGER,
  attribute2_id INTEGER, attribute2_value INTEGER, affinity REAL, defense INTEGER,
  slots INTEGER, production_price INTEGER, upgrade_price INTEGER,
  sharp_red INTEGER, sharp_orange INTEGER, sharp_yellow INTEGER, sharp_green INTEGER,
  sharp_blue INTEGER, sharp_white INTEGER, sharp_purple INTEGER,
  sharpness_plus INTEGER, gunlance_type INTEGER, switch_axe_phial INTEGER,
  hunting_note1 INTEGER, hunting_note2 INTEGER, hunting_note3 INTEGER,
  gun_reload INTEGER, gun_steadiness INTEGER, gun_recoil INTEGER,
  bow_shot INTEGER, bow_charge1 INTEGER, bow_charge2 INTEGER,
  bow_charge3 INTEGER, bow_charge4 INTEGER,
  UNIQUE(save_type,save_id)
);
CREATE TABLE armors(
  row_id INTEGER PRIMARY KEY, save_type INTEGER, save_id INTEGER, dex_id INTEGER UNIQUE,
  name_cn TEXT NOT NULL, name_en TEXT NOT NULL, name_jp TEXT NOT NULL DEFAULT '',
  is_placeholder INTEGER NOT NULL, mapping_status TEXT NOT NULL, mapping_source TEXT NOT NULL,
  combat INTEGER, gender INTEGER, rarity INTEGER, slots INTEGER,
  base_defense INTEGER, max_defense INTEGER, max_upgrade_level INTEGER,
  price INTEGER, fire_res INTEGER, water_res INTEGER, ice_res INTEGER,
  thunder_res INTEGER, dragon_res INTEGER,
  UNIQUE(save_type,save_id)
);
CREATE TABLE skill_trees(
  id INTEGER PRIMARY KEY, name_cn TEXT NOT NULL, name_en TEXT NOT NULL,
  name_jp TEXT NOT NULL DEFAULT '', mapping_status TEXT NOT NULL
);
CREATE TABLE active_skills(
  id INTEGER PRIMARY KEY, skill_tree_id INTEGER NOT NULL REFERENCES skill_trees(id),
  points INTEGER NOT NULL, name_cn TEXT NOT NULL, name_en TEXT NOT NULL,
  name_jp TEXT NOT NULL DEFAULT ''
);
CREATE TABLE armor_skill_points(
  armor_dex_id INTEGER NOT NULL REFERENCES armors(dex_id),
  skill_tree_id INTEGER NOT NULL REFERENCES skill_trees(id), points INTEGER NOT NULL,
  PRIMARY KEY(armor_dex_id,skill_tree_id)
);
CREATE TABLE decorations(
  dex_id INTEGER PRIMARY KEY, save_id INTEGER, item_dex_id INTEGER NOT NULL,
  name_cn TEXT NOT NULL, name_en TEXT NOT NULL, slots INTEGER NOT NULL,
  price INTEGER NOT NULL, mapping_status TEXT NOT NULL, mapping_source TEXT NOT NULL
);
CREATE TABLE save_decorations(
  save_id INTEGER PRIMARY KEY, name_cn TEXT NOT NULL, name_en TEXT NOT NULL,
  source TEXT NOT NULL, mapping_status TEXT NOT NULL
);
CREATE TABLE decoration_skill_points(
  decoration_dex_id INTEGER NOT NULL REFERENCES decorations(dex_id),
  skill_tree_id INTEGER NOT NULL REFERENCES skill_trees(id), points INTEGER NOT NULL,
  PRIMARY KEY(decoration_dex_id,skill_tree_id)
);
CREATE TABLE charm_classes(
  save_id INTEGER PRIMARY KEY, name_cn TEXT NOT NULL, name_en TEXT NOT NULL,
  source TEXT NOT NULL
);
CREATE TABLE charm_combinations(
  class_id INTEGER NOT NULL REFERENCES charm_classes(save_id), slots INTEGER NOT NULL,
  skill1_id INTEGER NOT NULL, skill1_points INTEGER NOT NULL,
  skill2_id INTEGER NOT NULL, skill2_points INTEGER NOT NULL,
  source_count INTEGER NOT NULL,
  PRIMARY KEY(class_id,slots,skill1_id,skill1_points,skill2_id,skill2_points)
);
CREATE INDEX idx_weapon_save ON weapons(save_type,save_id);
CREATE INDEX idx_armor_save ON armors(save_type,save_id);
CREATE INDEX idx_decoration_save ON decorations(save_id);
CREATE INDEX idx_charm_lookup ON charm_combinations(class_id,slots,skill1_id,skill1_points,skill2_id,skill2_points);
CREATE INDEX idx_armor_skill_filter ON armor_skill_points(skill_tree_id,points,armor_dex_id);
CREATE INDEX idx_charm_skill1_filter ON charm_combinations(skill1_id,skill1_points);
CREATE INDEX idx_charm_skill2_filter ON charm_combinations(skill2_id,skill2_points);
"""


def build(dex: Path, output: Path, manifest_path: Path) -> None:
    paths = required_inputs(dex)
    missing = [str(path) for path in paths.values() if not path.is_file()]
    if missing:
        raise ValueError("missing Dex inputs: " + ", ".join(missing))
    if not CROSSWALK.is_file() or not ARMOR_NATIVE.is_file():
        raise ValueError("missing static or native armor crosswalk")

    crosswalk = json.loads(CROSSWALK.read_text(encoding="utf-8"))
    if crosswalk.get("format") != "mh3g-static-crosswalk-v1":
        raise ValueError("unsupported static crosswalk format")
    tables: dict[str, list[dict[str, object]]] = crosswalk["legacy_tables"]
    armor_native = json.loads(ARMOR_NATIVE.read_text(encoding="utf-8"))
    if armor_native.get("format") != "mh3g-armor-native-parameters-v1":
        raise ValueError("unsupported native armor parameter format")
    if armor_native.get("source", {}).get("sha256") != EXEFS_SHA256:
        raise ValueError("native armor parameter source hash mismatch")
    native_by_save = {
        (int(row["save_type"]), int(row["save_id"])): row
        for row in armor_native["records"]
    }
    if len(native_by_save) != 1873:
        raise ValueError("native armor parameters must cover all 1873 save-local records")

    weapon_names = {int(row["Wpn_ID"]): row for row in read_csv(paths["weapon_names"]) if int(row["Wpn_ID"]) > 0}
    weapon_attrs = read_csv(paths["weapon_attributes"])
    armor_names = {int(row["Amr_ID"]): row for row in read_csv(paths["armor_names"]) if int(row["Amr_ID"]) > 0}
    armor_attrs = {int(row["Amr_ID"]): row for row in read_csv(paths["armor_attributes"])}
    armor_crosswalk = {int(row["dex_id"]): row for row in crosswalk["armor_dex_crosswalk"]}
    if len(weapon_attrs) != 1421 or len(armor_attrs) != 1651 or len(armor_crosswalk) != 1651:
        raise ValueError("unexpected Dex weapon/armor counts")

    # Resolve all Dex weapons against the save-local, dense ID arrays.
    weapon_by_save: dict[tuple[int, int], tuple[dict[str, str], str]] = {}
    for row in weapon_attrs:
        dex_id = int(row["Wpn_ID"])
        dex_type = int(row["Wpn_Type_ID"])
        save_type, table = TYPE_MAP[dex_type]
        names = weapon_names[dex_id]
        override = crosswalk["weapon_overrides"].get(str(dex_id))
        override_id = int(override["save_id"]) if override else None
        save_id, source = resolve_exact(tables[table], names["Wpn_Name_1"], names["Wpn_Name_0"], override_id)
        if save_id is None:
            raise ValueError(f"weapon Dex ID {dex_id} has no exact save mapping")
        if override and int(override["save_type"]) != save_type:
            raise ValueError(f"weapon Dex ID {dex_id} override has wrong save type")
        key = (save_type, save_id)
        if key in weapon_by_save:
            raise ValueError(f"duplicate weapon mapping {key}")
        weapon_by_save[key] = (row, source)
    if len(weapon_by_save) != 1421:
        raise ValueError("all 1421 weapons must map one-to-one")

    item_names = {int(row["Itm_ID"]): row for row in read_csv(paths["item_names"]) if int(row["Itm_ID"]) >= 0}
    jewel_rows = tables["jewels"]
    jewel_cn, jewel_en = exact_indexes(jewel_rows)
    decoration_rows = read_csv(paths["decorations"])
    decoration_save: dict[int, tuple[int | None, str]] = {}
    for row in decoration_rows:
        dex_id = int(row["Jew_ID"])
        names = item_names[int(row["Itm_ID"])]
        matches = set(jewel_cn.get(normalized(names["Itm_Name_1"]), []))
        matches.update(jewel_en.get(normalized(names["Itm_Name_0"]), []))
        if len(matches) == 1:
            decoration_save[dex_id] = (next(iter(matches)), "exact-save-local-name")
        else:
            decoration_save[dex_id] = (None, "ambiguous-or-unmapped-save-local-name")

    output.parent.mkdir(parents=True, exist_ok=True)
    manifest_path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="mh3g-sqlite-", dir=output.parent) as temporary:
        stage = Path(temporary) / "mh3g.sqlite"
        db = sqlite3.connect(str(stage))
        try:
            db.executescript(SCHEMA)
            db.executemany("INSERT INTO meta VALUES(?,?)", [
                ("format", FORMAT), ("generator_version", VERSION),
                ("game", "MH3G / MH3U"), ("id_authority", "ID_res.arc array index"),
            ])
            sources = [("ID_res.arc", crosswalk["authority"]["id_res_sha256"], "save ID authority")]
            sources.append(("MH3G ExeFS .code", EXEFS_SHA256,
                            "audited five-table source for save-local armor parameters"))
            sources.extend((name, sha256(path), "MH3G Dex runtime export") for name, path in sorted(paths.items()))
            sources.append(("mh3g_static_crosswalk.json", sha256(CROSSWALK), "reviewed save-ID crosswalk and legacy display names"))
            sources.append(("mh3g_armor_native_parameters.json", sha256(ARMOR_NATIVE),
                            "offline export of all 1873 native 24-byte armor records"))
            db.executemany("INSERT INTO sources VALUES(?,?,?)", sources)

            for kind, table in (("face", "faces"), ("hair", "hairs"), ("sex", "sexs"), ("voice", "voices")):
                db.executemany("INSERT INTO character_options VALUES(?,?,?,?,?)", [
                    (kind, int(row["id"]), row["name"], row["english"], row["source"])
                    for row in tables[table]
                ])
            db.executemany("INSERT INTO items VALUES(?,?,?,?,?)", [
                (int(row["id"]), row["name"], row["english"], row["source"], placeholder(row["name"], row["english"]))
                for row in tables["items"]
            ])
            for row in tables["equipment_types"]:
                save_type = int(row["id"])
                subtype = "armor" if 1 <= save_type <= 5 else "charm" if save_type == 6 else "weapon"
                db.execute("INSERT INTO equipment_types VALUES(?,?,?,?,?)", (save_type, subtype, row["name"], row["english"], row["source"]))

            weapon_row_id = 0
            for dex_type, (save_type, table) in sorted(TYPE_MAP.items()):
                for legacy in tables[table]:
                    weapon_row_id += 1
                    save_id = int(legacy["id"])
                    mapped = weapon_by_save.get((save_type, save_id))
                    values: tuple[object, ...]
                    if mapped is None:
                        values = (weapon_row_id, save_type, save_id, None, legacy["name"], legacy["english"], "",
                                  int(save_id == 0 or placeholder(legacy["name"], legacy["english"])), "placeholder", legacy["source"]) + (None,) * 32
                    else:
                        attr, source = mapped
                        dex_id = int(attr["Wpn_ID"])
                        names = weapon_names[dex_id]
                        sharp = sharpness(attr["Sharp"])
                        values = (
                            weapon_row_id, save_type, save_id, dex_id,
                            names["Wpn_Name_1"], names["Wpn_Name_0"], names["Wpn_Name_3"], 0, "confirmed", source,
                            int(attr["Rare"]), int(attr["Atk"]), int(attr["SpAtk1_ID"]), int(attr["SpAtk1_Pt"]),
                            int(attr["SpAtk2_ID"]), int(attr["SpAtk2_Pt"]), float(attr["Affinity"]), int(attr["Def"]),
                            int(attr["Slot"]), int(attr["ProPx"]), int(attr["LvUpPx"]), *sharp, int(attr["SharpP1"]),
                            int(attr["GLShotType_ID"]), int(attr["AxePhial_ID"]), int(attr["HHNote1_ID"]),
                            int(attr["HHNote2_ID"]), int(attr["HHNote3_ID"]), int(attr["GunReloadSpd_ID"]),
                            int(attr["GunSteadiness_ID"]), int(attr["GunRecoil_ID"]), int(attr["BowShot_ID"]),
                            int(attr["BowShotType1_ID"]), int(attr["BowShotType2_ID"]),
                            int(attr["BowShotType3_ID"]), int(attr["BowShotType4_ID"]),
                        )
                    db.execute("INSERT INTO weapons VALUES(" + ",".join("?" for _ in values) + ")", values)

            armor_dex_by_save = {
                (int(row["save_type"]), int(row["save_id"])): dex_id
                for dex_id, row in armor_crosswalk.items() if row["save_id"] is not None
            }
            armor_row_id = 0
            inserted_dex: set[int] = set()
            for save_type, table in sorted(ARMOR_TABLES.items()):
                for legacy in tables[table]:
                    armor_row_id += 1
                    save_id = int(legacy["id"])
                    native = native_by_save[(save_type, save_id)]
                    dex_id = armor_dex_by_save.get((save_type, save_id))
                    if dex_id is None:
                        is_placeholder = int(save_id == 0 or placeholder(legacy["name"], legacy["english"]))
                        native_present = native["evidence"] != "empty_native_record"
                        status = "placeholder" if is_placeholder else "confirmed_mh3g" if native_present else "unknown"
                        source = "ID_res+MH3G-ExeFS-native" if native_present else "ID_res-only; empty native record"
                        values = (armor_row_id, save_type, save_id, None, legacy["name"], legacy["english"], "",
                                  is_placeholder, status, source,
                                  native["combat"], native["gender"], native["rarity"], native["slots"],
                                  native["base_defense"], None, None, None,
                                  native["fire_res"], native["water_res"], native["ice_res"],
                                  native["thunder_res"], native["dragon_res"])
                    else:
                        attr = armor_attrs[dex_id]
                        names = armor_names[dex_id]
                        inserted_dex.add(dex_id)
                        values = (
                            armor_row_id, save_type, save_id, dex_id,
                            names["Amr_Name_1"], names["Amr_Name_0"], names["Amr_Name_3"], 0,
                            "confirmed", armor_crosswalk[dex_id]["mapping_source"] + "+MH3G-ExeFS-native",
                            native["combat"], native["gender"], native["rarity"], native["slots"],
                            native["base_defense"], int(attr["MaxDef"]), None, int(attr["Price"]),
                            native["fire_res"], native["water_res"], native["ice_res"],
                            native["thunder_res"], native["dragon_res"],
                        )
                    db.execute("INSERT INTO armors VALUES(" + ",".join("?" for _ in values) + ")", values)
            for dex_id in sorted(set(armor_attrs) - inserted_dex):
                armor_row_id += 1
                attr, names = armor_attrs[dex_id], armor_names[dex_id]
                save_type = ARMOR_PART[int(attr["Part"])]
                values = (
                    armor_row_id, None, None, dex_id, names["Amr_Name_1"], names["Amr_Name_0"], names["Amr_Name_3"], 0,
                    "unknown", "Dex entry has no confirmed save-local ID", int(attr["BorG"]), int(attr["MorF"]),
                    int(attr["Rare"]), int(attr["Slot"]), int(attr["Def"]), int(attr["MaxDef"]), None,
                    int(attr["Price"]), int(attr["Res_Fire"]), int(attr["Res_Water"]), int(attr["Res_Ice"]),
                    int(attr["Res_Thunder"]), int(attr["Res_Dragon"]),
                )
                db.execute("INSERT INTO armors VALUES(" + ",".join("?" for _ in values) + ")", values)

            dex_skill_names = {int(row["SklTree_ID"]): row for row in read_csv(paths["skill_tree_names"]) if int(row["SklTree_ID"]) > 0}
            legacy_skills = {int(row["id"]): row for row in tables["skills"]}
            for skill_id in sorted(legacy_skills):
                legacy = legacy_skills[skill_id]
                dex_name = dex_skill_names.get(skill_id)
                db.execute("INSERT INTO skill_trees VALUES(?,?,?,?,?)", (
                    skill_id, dex_name["SklTree_Name_1"] if dex_name else legacy["name"],
                    dex_name["SklTree_Name_0"] if dex_name else legacy["english"],
                    dex_name["SklTree_Name_3"] if dex_name else "", "confirmed" if dex_name else "save-local-only",
                ))
            active_names = {int(row["Skl_ID"]): row for row in read_csv(paths["active_skill_names"]) if int(row["Skl_ID"]) > 0}
            for row in read_csv(paths["active_skills"]):
                skill_id = int(row["Skl_ID"]); names = active_names[skill_id]
                db.execute("INSERT INTO active_skills VALUES(?,?,?,?,?,?)", (
                    skill_id, int(row["SklTree_ID"]), int(row["Pt"]), names["Skl_Name_1"], names["Skl_Name_0"], names["Skl_Name_3"]
                ))
            for row in read_csv(paths["armor_skill_points"]):
                dex_id = int(row["Amr_ID"])
                db.execute("INSERT INTO armor_skill_points VALUES(?,?,?)", (dex_id, int(row["SklTree_ID"]), int(row["Pt"])))

            for row in decoration_rows:
                dex_id = int(row["Jew_ID"]); item_id = int(row["Itm_ID"]); names = item_names[item_id]
                save_id, source = decoration_save[dex_id]
                db.execute("INSERT INTO decorations VALUES(?,?,?,?,?,?,?,?,?)", (
                    dex_id, save_id, item_id, names["Itm_Name_1"], names["Itm_Name_0"], int(row["Slot"]),
                    int(row["Price"]), "confirmed" if save_id is not None else "unknown", source,
                ))
            confirmed_decoration_ids = {value[0] for value in decoration_save.values() if value[0] is not None}
            for row in jewel_rows:
                save_id = int(row["id"])
                db.execute("INSERT INTO save_decorations VALUES(?,?,?,?,?)", (
                    save_id, row["name"], row["english"], row["source"],
                    "confirmed" if save_id in confirmed_decoration_ids else "unknown",
                ))
            for row in read_csv(paths["decoration_skill_points"]):
                db.execute("INSERT INTO decoration_skill_points VALUES(?,?,?)", (
                    int(row["Jew_ID"]), int(row["SklTree_ID"]), int(row["Pt"])
                ))

            for row in tables["charms"]:
                db.execute("INSERT INTO charm_classes VALUES(?,?,?,?)", (int(row["id"]), row["name"], row["english"], row["source"]))
            charm_counts: dict[tuple[int, int, int, int, int, int], int] = defaultdict(int)
            for row in read_csv(paths["charms"]):
                skill1 = int(row["SklTree1_ID"] or 0)
                skill2 = int(row["SklTree2_ID"] or 0)
                key = (int(row["Tls_Lv_ID"]), int(row["Slot"]),
                       max(skill1, 0), int(row["SklTree1_Pt"] or 0) if skill1 > 0 else 0,
                       max(skill2, 0), int(row["SklTree2_Pt"] or 0) if skill2 > 0 else 0)
                charm_counts[key] += 1
            db.executemany("INSERT INTO charm_combinations VALUES(?,?,?,?,?,?,?)", [(*key, count) for key, count in sorted(charm_counts.items())])

            db.commit()
            if db.execute("PRAGMA integrity_check").fetchone()[0] != "ok":
                raise ValueError("SQLite integrity check failed")
            if db.execute("PRAGMA foreign_key_check").fetchone() is not None:
                raise ValueError("SQLite foreign-key check failed")
            db.execute("VACUUM")
        finally:
            db.close()
        stage.replace(output)

    connection = sqlite3.connect(str(output))
    try:
        counts = {
            table: connection.execute(f"SELECT count(*) FROM {table}").fetchone()[0]
            for table in ("character_options", "items", "equipment_types", "weapons", "armors", "skill_trees",
                          "active_skills", "armor_skill_points", "decorations", "save_decorations", "decoration_skill_points",
                          "charm_classes", "charm_combinations")
        }
        logical_hashes = {}
        for table in sorted(counts):
            rows = connection.execute(f"SELECT * FROM {table} ORDER BY rowid").fetchall()
            payload = json.dumps(rows, ensure_ascii=False, separators=(",", ":"), default=str).encode()
            logical_hashes[table] = hashlib.sha256(payload).hexdigest()
    finally:
        connection.close()
    manifest = {
        "format": "mh3g-save-editor-data-manifest-v1", "generator_version": VERSION,
        "database": {"file": output.name, "sha256": sha256(output), "bytes": output.stat().st_size, "user_version": DB_VERSION},
        "sources": {name: {"sha256": sha256(path), "bytes": path.stat().st_size} for name, path in sorted(paths.items())},
        "crosswalks": {
            "save_ids": {"file": CROSSWALK.name, "sha256": sha256(CROSSWALK)},
            "armor_native": {"file": ARMOR_NATIVE.name, "sha256": sha256(ARMOR_NATIVE)},
        },
        "counts": counts, "logical_hashes": logical_hashes,
        "notes": {
            "save_id_authority": "ID_res.arc array index; Dex primary keys are metadata only",
            "armor_unknown": "empty native records and Dex entries without save IDs remain unknown",
            "exefs_expected_sha256": EXEFS_SHA256,
            "armor_upgrade_limit": "not decoded from the audited native tables; remains unknown",
            "decoration_mapping": "only unique exact matches to the save-local decoration array are confirmed",
            "charm_policy": "all-game obtainable combinations, deduplicated from 261448 generation rows",
        },
    }
    manifest_path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8", newline="\n")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dex-dump", required=True, type=Path)
    parser.add_argument("--output", type=Path, default=ROOT / "data/mh3g.sqlite")
    parser.add_argument("--manifest", type=Path, default=ROOT / "data/manifest.json")
    args = parser.parse_args()
    build(args.dex_dump.resolve(), args.output.resolve(), args.manifest.resolve())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
