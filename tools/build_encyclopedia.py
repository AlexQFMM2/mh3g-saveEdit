#!/usr/bin/env python3
"""Build the deterministic MH3G weapon and armor encyclopedia database.

The Dex dump supplies presentation attributes and relationships.  Save IDs
are resolved independently against the audited ID_res-derived CSV arrays.
Raw Dex files remain outside the repository.
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


VERSION = "2.0.0"
ROOT = Path(__file__).resolve().parents[1]
CROSSWALK = Path(__file__).with_name("mh3g_encyclopedia_crosswalk.json")
MODEL_CROSSWALK = Path(__file__).with_name("mh3g_weapon_model_crosswalk.json")
ARMOR_SETS = Path(__file__).with_name("mh3g_armor_sets.csv")
ARMOR_MEMBERS = Path(__file__).with_name("mh3g_armor_set_members.csv")
ARMOR_MODELS = Path(__file__).with_name("mh3g_armor_model_resources.csv")
TYPE_MAP = {
    1: (7, "gs_weapons", "great-sword", "大剑", "Great Sword"),
    2: (14, "ls_weapons", "long-sword", "太刀", "Long Sword"),
    3: (8, "sns_weapons", "sword-shield", "片手剑", "Sword & Shield"),
    4: (18, "db_weapons", "dual-blades", "双剑", "Dual Blades"),
    5: (9, "h_weapons", "hammer", "大锤", "Hammer"),
    6: (19, "hh_weapons", "hunting-horn", "狩猎笛", "Hunting Horn"),
    7: (10, "l_weapons", "lance", "长枪", "Lance"),
    8: (16, "gl_weapons", "gunlance", "铳枪", "Gunlance"),
    9: (15, "sa_weapons", "switch-axe", "斩击斧", "Switch Axe"),
    10: (13, "lbg_weapons", "light-bowgun", "轻弩", "Light Bowgun"),
    11: (11, "hbg_weapons", "heavy-bowgun", "重弩", "Heavy Bowgun"),
    12: (17, "bow_weapons", "bow", "弓", "Bow"),
}
ARMOR_PARTS = {
    1: ("head", 5), 2: ("chest", 1), 3: ("arms", 2), 4: ("waist", 3), 5: ("legs", 4),
}
ARMOR_COMBAT = {0: "both", 1: "blade", 2: "gunner"}
ARMOR_GENDER = {0: "both", 1: "male", 2: "female"}


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
    # Keep '+' because it distinguishes adjacent upgrade forms.  Full-width
    # plus is already normalized by NFKC.
    return re.sub(r"[\s・·【】\[\]()（）,，:：\"“”'‘’._-]", "", value)


def exact_index(rows: list[dict[str, str]]) -> tuple[dict[str, list[int]], dict[str, list[int]]]:
    english: dict[str, list[int]] = defaultdict(list)
    chinese: dict[str, list[int]] = defaultdict(list)
    for row in rows:
        identifier = int(row["id"])
        english[normalized(row["english"])].append(identifier)
        chinese[normalized(row["name"])].append(identifier)
    return english, chinese


def resolve(
    english: dict[str, list[int]],
    chinese: dict[str, list[int]],
    name_en: str,
    name_cn: str,
    explicit: int | None,
) -> tuple[int | None, str]:
    en_matches = english.get(normalized(name_en), [])
    if len(en_matches) == 1:
        return en_matches[0], "exact-en"
    cn_matches = chinese.get(normalized(name_cn), [])
    if len(cn_matches) == 1:
        return cn_matches[0], "exact-cn"
    if explicit is not None:
        return explicit, "reviewed-crosswalk"
    return None, "unmapped"


def parse_sharpness(value: str) -> list[int]:
    if not re.fullmatch(r"\d{14}", value):
        raise ValueError(f"invalid sharpness {value!r}")
    # Dex stores purple -> red.  Normalize to the UI order red -> purple.
    return list(reversed([int(value[index:index + 2]) for index in range(0, 14, 2)]))


def create_schema(db: sqlite3.Connection) -> None:
    db.executescript(
        """
        PRAGMA page_size=4096;
        PRAGMA encoding='UTF-8';
        PRAGMA journal_mode=OFF;
        PRAGMA synchronous=OFF;
        PRAGMA foreign_keys=ON;
        CREATE TABLE meta(key TEXT PRIMARY KEY, value TEXT NOT NULL);
        CREATE TABLE weapon_types(
          dex_type INTEGER PRIMARY KEY, save_type INTEGER NOT NULL UNIQUE,
          slug TEXT NOT NULL UNIQUE, name_cn TEXT NOT NULL, name_en TEXT NOT NULL,
          display_order INTEGER NOT NULL UNIQUE
        );
        CREATE TABLE special_attributes(
          id INTEGER PRIMARY KEY, name_cn TEXT NOT NULL, name_en TEXT NOT NULL, name_jp TEXT NOT NULL
        );
        CREATE TABLE items(
          dex_id INTEGER PRIMARY KEY, save_id INTEGER UNIQUE,
          name_cn TEXT NOT NULL, name_en TEXT NOT NULL, name_jp TEXT NOT NULL,
          rarity INTEGER, max_count INTEGER, sell_price INTEGER, buy_price INTEGER,
          writable INTEGER NOT NULL, mapping_source TEXT NOT NULL
        );
        CREATE TABLE weapons(
          dex_id INTEGER PRIMARY KEY, dex_type INTEGER NOT NULL REFERENCES weapon_types(dex_type),
          save_type INTEGER, save_id INTEGER, display_order INTEGER NOT NULL,
          name_cn TEXT NOT NULL, name_en TEXT NOT NULL, name_jp TEXT NOT NULL,
          rarity INTEGER NOT NULL, attack INTEGER NOT NULL,
          attribute1_id INTEGER, attribute1_value INTEGER,
          attribute2_id INTEGER, attribute2_value INTEGER,
          affinity REAL NOT NULL, defense INTEGER NOT NULL, slots INTEGER NOT NULL,
          production_price INTEGER NOT NULL, upgrade_price INTEGER NOT NULL,
          sharp_red INTEGER NOT NULL, sharp_orange INTEGER NOT NULL,
          sharp_yellow INTEGER NOT NULL, sharp_green INTEGER NOT NULL,
          sharp_blue INTEGER NOT NULL, sharp_white INTEGER NOT NULL,
          sharp_purple INTEGER NOT NULL, sharp_plus INTEGER NOT NULL,
          gunlance_type INTEGER, switch_axe_phial INTEGER,
          hunting_note1 INTEGER, hunting_note2 INTEGER, hunting_note3 INTEGER,
          gun_reload INTEGER, gun_steadiness INTEGER, gun_recoil INTEGER,
          bow_shot INTEGER, bow_charge1 INTEGER, bow_charge2 INTEGER,
          bow_charge3 INTEGER, bow_charge4 INTEGER,
          image_key TEXT NOT NULL, gallery_type INTEGER NOT NULL,
          writable INTEGER NOT NULL, mapping_source TEXT NOT NULL,
          UNIQUE(save_type, save_id)
        );
        CREATE TABLE weapon_roots(
          dex_type INTEGER NOT NULL REFERENCES weapon_types(dex_type),
          weapon_dex_id INTEGER PRIMARY KEY REFERENCES weapons(dex_id)
        );
        CREATE TABLE weapon_edges(
          parent_dex_id INTEGER NOT NULL REFERENCES weapons(dex_id),
          child_dex_id INTEGER NOT NULL REFERENCES weapons(dex_id),
          PRIMARY KEY(parent_dex_id, child_dex_id)
        );
        CREATE TABLE weapon_materials(
          weapon_dex_id INTEGER NOT NULL REFERENCES weapons(dex_id),
          item_dex_id INTEGER NOT NULL REFERENCES items(dex_id),
          quantity INTEGER NOT NULL CHECK(quantity > 0),
          kind TEXT NOT NULL CHECK(kind IN ('production','upgrade')),
          region TEXT NOT NULL,
          PRIMARY KEY(weapon_dex_id, item_dex_id, kind, region)
        );
        CREATE TABLE model_resources(
          model_key TEXT PRIMARY KEY,
          arc_relative_path TEXT NOT NULL UNIQUE,
          mod_selector TEXT NOT NULL,
          mrl_selector TEXT NOT NULL,
          texture_selector TEXT NOT NULL,
          mapping_source TEXT NOT NULL
        );
        CREATE TABLE weapon_models(
          weapon_dex_id INTEGER PRIMARY KEY REFERENCES weapons(dex_id),
          model_key TEXT NOT NULL REFERENCES model_resources(model_key),
          mapping_status TEXT NOT NULL CHECK(mapping_status IN ('confirmed','unmapped')),
          mapping_source TEXT NOT NULL
        );
        CREATE TABLE skill_trees(
          id INTEGER PRIMARY KEY, name_cn TEXT NOT NULL, name_en TEXT NOT NULL, name_jp TEXT NOT NULL
        );
        CREATE TABLE active_skills(
          id INTEGER PRIMARY KEY, skill_tree_id INTEGER NOT NULL REFERENCES skill_trees(id),
          points INTEGER NOT NULL, name_cn TEXT NOT NULL, name_en TEXT NOT NULL, name_jp TEXT NOT NULL
        );
        CREATE TABLE armor_sets(
          set_id TEXT PRIMARY KEY, rank TEXT NOT NULL CHECK(rank IN ('low','high','g','special')),
          combat TEXT NOT NULL CHECK(combat IN ('both','blade','gunner')), model_id INTEGER NOT NULL,
          name_cn TEXT NOT NULL, name_en TEXT NOT NULL, display_order INTEGER NOT NULL UNIQUE,
          review_status TEXT NOT NULL, source TEXT NOT NULL, notes TEXT NOT NULL
        );
        CREATE TABLE armors(
          dex_id INTEGER PRIMARY KEY, save_type INTEGER, save_id INTEGER,
          part TEXT NOT NULL CHECK(part IN ('head','chest','arms','waist','legs')),
          combat TEXT NOT NULL CHECK(combat IN ('both','blade','gunner')),
          gender TEXT NOT NULL CHECK(gender IN ('both','male','female')),
          name_cn TEXT NOT NULL, name_en TEXT NOT NULL, name_jp TEXT NOT NULL,
          rarity INTEGER NOT NULL, slots INTEGER NOT NULL, defense INTEGER NOT NULL, max_defense INTEGER NOT NULL,
          price INTEGER NOT NULL, fire_res INTEGER NOT NULL, water_res INTEGER NOT NULL,
          ice_res INTEGER NOT NULL, thunder_res INTEGER NOT NULL, dragon_res INTEGER NOT NULL,
          writable INTEGER NOT NULL, mapping_source TEXT NOT NULL,
          UNIQUE(save_type, save_id)
        );
        CREATE TABLE armor_set_members(
          set_id TEXT NOT NULL REFERENCES armor_sets(set_id), armor_dex_id INTEGER NOT NULL UNIQUE REFERENCES armors(dex_id),
          gender TEXT NOT NULL CHECK(gender IN ('both','male','female')), slot_order INTEGER NOT NULL,
          PRIMARY KEY(set_id, armor_dex_id), UNIQUE(set_id, slot_order)
        );
        CREATE TABLE armor_materials(
          armor_dex_id INTEGER NOT NULL REFERENCES armors(dex_id), item_dex_id INTEGER NOT NULL REFERENCES items(dex_id),
          quantity INTEGER NOT NULL CHECK(quantity > 0), PRIMARY KEY(armor_dex_id, item_dex_id)
        );
        CREATE TABLE armor_skill_points(
          armor_dex_id INTEGER NOT NULL REFERENCES armors(dex_id), skill_tree_id INTEGER NOT NULL REFERENCES skill_trees(id),
          points INTEGER NOT NULL, PRIMARY KEY(armor_dex_id, skill_tree_id)
        );
        CREATE TABLE armor_model_resources(
          model_key TEXT PRIMARY KEY, model_id INTEGER NOT NULL,
          gender TEXT NOT NULL CHECK(gender IN ('male','female')),
          part TEXT NOT NULL CHECK(part IN ('head','chest','arms','waist','legs')),
          arc_relative_path TEXT NOT NULL UNIQUE, mapping_source TEXT NOT NULL,
          UNIQUE(model_id, gender, part)
        );
        CREATE INDEX idx_weapons_type_order ON weapons(dex_type, display_order);
        CREATE INDEX idx_material_item ON weapon_materials(item_dex_id, weapon_dex_id);
        CREATE INDEX idx_edges_child ON weapon_edges(child_dex_id);
        CREATE INDEX idx_armor_set_order ON armor_sets(rank, display_order);
        CREATE INDEX idx_armor_material_item ON armor_materials(item_dex_id, armor_dex_id);
        CREATE INDEX idx_armor_skill_tree ON armor_skill_points(skill_tree_id, armor_dex_id);
        """
    )


def build(dex_root: Path, data_root: Path, output: Path, manifest_path: Path) -> None:
    paths = {
        "manifest": dex_root / "manifest.json",
        "weapon_names": dex_root / "tables" / "004_u1704.csv",
        "attributes": dex_root / "tables" / "014_u170e.csv",
        "weapons": dex_root / "tables" / "018_u1712.csv",
        "items": dex_root / "direct_sql" / "items_id_zh_en.csv",
        "item_attributes": dex_root / "direct_sql" / "DB_Itm.csv",
        "materials": dex_root / "direct_sql" / "DB_ItmtoWpn.csv",
        "armor_names": dex_root / "tables" / "022_u1716.csv",
        "armors": dex_root / "tables" / "021_u1715.csv",
        "armor_materials": dex_root / "direct_sql" / "DB_ItmtoAmr.csv",
        "armor_skill_points": dex_root / "direct_sql" / "DB_SklTreetoAmr.csv",
        "skill_tree_names": dex_root / "tables" / "007_u1707.csv",
        "active_skills": dex_root / "tables" / "019_u1713.csv",
        "active_skill_names": dex_root / "tables" / "020_u1714.csv",
        "armor_sets": ARMOR_SETS,
        "armor_members": ARMOR_MEMBERS,
        "armor_models": ARMOR_MODELS,
    }
    missing = [str(path) for path in paths.values() if not path.is_file()]
    if missing:
        raise ValueError("missing Dex inputs: " + ", ".join(missing))

    dex_manifest = json.loads(paths["manifest"].read_text(encoding="utf-8-sig"))
    table_meta = {entry["csvFile"]: entry for entry in dex_manifest["tables"]}
    if table_meta["018_u1712.csv"]["rowCount"] != 1421:
        raise ValueError("Dex weapon table must contain 1421 rows")
    if table_meta["021_u1715.csv"]["rowCount"] != 1651:
        raise ValueError("Dex armor table must contain 1651 rows")

    crosswalk = json.loads(CROSSWALK.read_text(encoding="utf-8"))
    if crosswalk.get("format") != "mh3g-encyclopedia-crosswalk-v1":
        raise ValueError("unsupported crosswalk format")
    explicit_weapons = {int(key): value for key, value in crosswalk["weapons"].items()}
    explicit_items = {int(key): value for key, value in crosswalk["items"].items()}
    model_crosswalk = json.loads(MODEL_CROSSWALK.read_text(encoding="utf-8"))
    if model_crosswalk.get("format") != "mh3g-weapon-model-crosswalk-v1":
        raise ValueError("unsupported weapon model crosswalk format")
    model_by_save: dict[tuple[int, int], tuple[str, str]] = {}
    for dex_type, values in sorted(((int(key), value) for key, value in model_crosswalk["types"].items())):
        folder = values["resource_folder"]
        model_ids: list[int] = []
        for model_id, count in values["model_id_runs"]:
            model_ids.extend([int(model_id)] * int(count))
        if len(model_ids) != int(values["save_id_count"]):
            raise ValueError(f"model crosswalk type {dex_type}: invalid run length")
        save_type = TYPE_MAP[dex_type][0]
        for save_id, model_id in enumerate(model_ids, 1):
            model_key = f"{folder}_{model_id:02d}"
            model_by_save[(save_type, save_id)] = (model_key, f"{folder}/{model_key}.arc")

    weapon_names = {int(row["Wpn_ID"]): row for row in read_csv(paths["weapon_names"]) if int(row["Wpn_ID"]) >= 0}
    weapon_rows = read_csv(paths["weapons"])
    item_names = {int(row["Itm_ID"]): row for row in read_csv(paths["items"]) if int(row["Itm_ID"]) >= 0}
    item_attributes = {int(row["Itm_ID"]): row for row in read_csv(paths["item_attributes"])}
    material_rows = read_csv(paths["materials"])
    armor_material_rows = read_csv(paths["armor_materials"])
    used_item_ids = sorted({int(row["Itm_ID"]) for row in material_rows + armor_material_rows})
    armor_rows = read_csv(paths["armors"])
    armor_attributes = {int(row["Amr_ID"]): row for row in armor_rows}
    armor_names = {int(row["Amr_ID"]): row for row in read_csv(paths["armor_names"]) if int(row["Amr_ID"]) > 0}
    armor_set_rows = read_csv(ARMOR_SETS)
    armor_member_rows = read_csv(ARMOR_MEMBERS)
    armor_model_rows = read_csv(ARMOR_MODELS)

    save_weapon_rows: dict[int, list[dict[str, str]]] = {}
    save_weapon_indexes = {}
    for dex_type, (_, table, _, _, _) in TYPE_MAP.items():
        rows = read_csv(data_root / "cn" / f"{table}.csv")
        save_weapon_rows[dex_type] = rows
        save_weapon_indexes[dex_type] = exact_index(rows)
    save_item_rows = read_csv(data_root / "cn" / "items.csv")
    save_item_by_id = {int(row["id"]): row for row in save_item_rows}
    save_item_indexes = exact_index(save_item_rows)

    resolved_weapons: dict[int, tuple[int | None, int | None, str]] = {}
    seen_save_weapons: set[tuple[int, int]] = set()
    per_type_order: dict[int, int] = defaultdict(int)
    for row in weapon_rows:
        dex_id = int(row["Wpn_ID"])
        dex_type = int(row["Wpn_Type_ID"])
        save_type = TYPE_MAP[dex_type][0]
        names = weapon_names[dex_id]
        explicit = explicit_weapons.get(dex_id)
        explicit_id = None
        if explicit:
            if int(explicit["save_type"]) != save_type:
                raise ValueError(f"weapon {dex_id}: explicit save type mismatch")
            explicit_id = int(explicit["save_id"])
        save_id, source = resolve(
            *save_weapon_indexes[dex_type], names["Wpn_Name_0"], names["Wpn_Name_1"], explicit_id
        )
        if save_id is not None:
            known_ids = {int(value["id"]) for value in save_weapon_rows[dex_type]}
            if save_id not in known_ids or save_id == 0:
                raise ValueError(f"weapon {dex_id}: invalid save ID {save_id}")
            key = (save_type, save_id)
            if key in seen_save_weapons:
                raise ValueError(f"weapon {dex_id}: duplicate save mapping {key}")
            seen_save_weapons.add(key)
        resolved_weapons[dex_id] = (save_type if save_id is not None else None, save_id, source)
        per_type_order[dex_type] += 1

    resolved_items: dict[int, tuple[int | None, str]] = {}
    seen_save_items: set[int] = set()
    for dex_id in used_item_ids:
        names = item_names[dex_id]
        explicit = explicit_items.get(dex_id)
        save_id, source = resolve(
            *save_item_indexes, names["Itm_Name_0"], names["Itm_Name_1"],
            int(explicit["save_id"]) if explicit else None,
        )
        if save_id is not None:
            if save_id not in save_item_by_id or save_id <= 1:
                raise ValueError(f"item {dex_id}: invalid save ID {save_id}")
            if save_id in seen_save_items:
                raise ValueError(f"item {dex_id}: duplicate save mapping {save_id}")
            seen_save_items.add(save_id)
        resolved_items[dex_id] = (save_id, source)

    known_set_ids = {row["set_id"] for row in armor_set_rows}
    if len(known_set_ids) != len(armor_set_rows):
        raise ValueError("duplicate armor set ID")
    member_by_dex: dict[int, dict[str, str]] = {}
    seen_armor_save: set[tuple[int, int]] = set()
    for member in armor_member_rows:
        dex_id = int(member["dex_id"])
        if member["set_id"] not in known_set_ids or dex_id in member_by_dex:
            raise ValueError(f"armor member {dex_id}: duplicate or unknown set")
        if dex_id not in armor_names:
            raise ValueError(f"armor member {dex_id}: missing Dex name")
        attr = armor_attributes.get(dex_id)
        if attr is None:
            raise ValueError(f"armor member {dex_id}: missing Dex attributes")
        part, expected_save_type = ARMOR_PARTS[int(attr["Part"])]
        if member["part"] != part or int(member["save_type"]) != expected_save_type:
            raise ValueError(f"armor member {dex_id}: part/save type mismatch")
        if member["gender"] != ARMOR_GENDER[int(attr["MorF"])]:
            raise ValueError(f"armor member {dex_id}: gender mismatch")
        if member["save_id"]:
            save_key = (expected_save_type, int(member["save_id"]))
            if save_key in seen_armor_save:
                raise ValueError(f"armor member {dex_id}: duplicate save mapping {save_key}")
            seen_armor_save.add(save_key)
        member_by_dex[dex_id] = member
    if sorted(member_by_dex) != list(range(1, 1652)):
        raise ValueError("armor members must cover Dex IDs 1..1651 exactly once")

    seen_model_keys: set[str] = set()
    seen_model_slots: set[tuple[int, str, str]] = set()
    for model in armor_model_rows:
        key = model["model_key"]
        slot = (int(model["model_id"]), model["gender"], model["part"])
        if key in seen_model_keys or slot in seen_model_slots:
            raise ValueError(f"duplicate armor model mapping {key} / {slot}")
        if model["gender"] not in {"male", "female"} or model["part"] not in {value[0] for value in ARMOR_PARTS.values()}:
            raise ValueError(f"invalid armor model mapping {key}")
        seen_model_keys.add(key)
        seen_model_slots.add(slot)

    output.parent.mkdir(parents=True, exist_ok=True)
    manifest_path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="mh3g-encyclopedia-", dir=output.parent) as directory:
        stage = Path(directory) / "encyclopedia.sqlite"
        db = sqlite3.connect(str(stage))
        try:
            create_schema(db)
            db.executemany("INSERT INTO meta VALUES(?,?)", [
                ("format", "mh3g-encyclopedia-v2"),
                ("generator_version", VERSION),
                ("game", "mh3g"),
            ])
            for order, (dex_type, values) in enumerate(TYPE_MAP.items()):
                save_type, _, slug, name_cn, name_en = values
                db.execute("INSERT INTO weapon_types VALUES(?,?,?,?,?,?)", (dex_type, save_type, slug, name_cn, name_en, order))

            for row in read_csv(paths["attributes"]):
                db.execute("INSERT INTO special_attributes VALUES(?,?,?,?)", (
                    int(row["Wpn_SpAtk_ID"]), row["Wpn_SpAtk_1"], row["Wpn_SpAtk_0"], row["Wpn_SpAtk_3"]
                ))

            for dex_id in used_item_ids:
                names = item_names[dex_id]
                attr = item_attributes.get(dex_id, {})
                save_id, source = resolved_items[dex_id]
                db.execute("INSERT INTO items VALUES(?,?,?,?,?,?,?,?,?,?,?)", (
                    dex_id, save_id, names["Itm_Name_1"], names["Itm_Name_0"], names["Itm_Name_3"],
                    int(attr["Rare"]) if attr.get("Rare") else None,
                    int(attr["Max"]) if attr.get("Max") else None,
                    int(attr["Sell"]) if attr.get("Sell") else None,
                    int(attr["Buy"]) if attr.get("Buy") else None,
                    int(save_id is not None), source,
                ))

            order_by_type: dict[int, int] = defaultdict(int)
            rows_by_type: dict[int, list[dict[str, str]]] = defaultdict(list)
            for row in weapon_rows:
                dex_id = int(row["Wpn_ID"])
                dex_type = int(row["Wpn_Type_ID"])
                names = weapon_names[dex_id]
                save_type, save_id, source = resolved_weapons[dex_id]
                sharp = parse_sharpness(row["Sharp"])
                order = order_by_type[dex_type]
                order_by_type[dex_type] += 1
                rows_by_type[dex_type].append(row)
                db.execute(
                    "INSERT INTO weapons VALUES(" + ",".join("?" for _ in range(44)) + ")",
                    (
                        dex_id, dex_type, save_type, save_id, order,
                        names["Wpn_Name_1"], names["Wpn_Name_0"], names["Wpn_Name_3"],
                        int(row["Rare"]), int(row["Atk"]), int(row["SpAtk1_ID"]), int(row["SpAtk1_Pt"]),
                        int(row["SpAtk2_ID"]), int(row["SpAtk2_Pt"]), float(row["Affinity"]), int(row["Def"]),
                        int(row["Slot"]), int(row["ProPx"]), int(row["LvUpPx"]), *sharp, int(row["SharpP1"]),
                        int(row["GLShotType_ID"]), int(row["AxePhial_ID"]), int(row["HHNote1_ID"]),
                        int(row["HHNote2_ID"]), int(row["HHNote3_ID"]), int(row["GunReloadSpd_ID"]),
                        int(row["GunSteadiness_ID"]), int(row["GunRecoil_ID"]), int(row["BowShot_ID"]),
                        int(row["BowShotType1_ID"]), int(row["BowShotType2_ID"]), int(row["BowShotType3_ID"]),
                        int(row["BowShotType4_ID"]), row["Wpn_ImgFile"], int(row["Wpn_GalType"]),
                        int(save_id is not None), source,
                    ),
                )

            for dex_type in sorted(rows_by_type):
                rows = rows_by_type[dex_type]
                index = 0
                def add_node(parent: int | None = None) -> int:
                    nonlocal index
                    if index >= len(rows):
                        raise ValueError(f"weapon type {dex_type}: child tree exceeds table")
                    row = rows[index]
                    index += 1
                    node = int(row["Wpn_ID"])
                    if parent is None:
                        db.execute("INSERT INTO weapon_roots VALUES(?,?)", (dex_type, node))
                    else:
                        db.execute("INSERT INTO weapon_edges VALUES(?,?)", (parent, node))
                    for _ in range(int(row["Child"])):
                        add_node(node)
                    return node
                while index < len(rows):
                    add_node()

            for row in sorted(material_rows, key=lambda value: int(value["ID"])):
                db.execute("INSERT INTO weapon_materials VALUES(?,?,?,?,?)", (
                    int(row["Wpn_ID"]), int(row["Itm_ID"]), int(row["Qty"]),
                    "production" if row["Type"] == "P" else "upgrade", row["Region"],
                ))

            model_resources: dict[str, str] = {}
            weapon_model_rows: list[tuple[int, str, str, str]] = []
            for dex_id, (save_type, save_id, _) in sorted(resolved_weapons.items()):
                if save_type is None or save_id is None:
                    continue
                model = model_by_save.get((save_type, save_id))
                if model is None:
                    raise ValueError(f"weapon {dex_id}: missing confirmed model mapping")
                model_key, arc_path = model
                previous = model_resources.setdefault(model_key, arc_path)
                if previous != arc_path:
                    raise ValueError(f"model {model_key}: conflicting ARC paths")
                weapon_model_rows.append((dex_id, model_key, "confirmed", "exefs-weapon-parameter-model-id"))
            for model_key, arc_path in sorted(model_resources.items()):
                db.execute("INSERT INTO model_resources VALUES(?,?,?,?,?,?)", (
                    model_key, arc_path, "type-hash:0x58a15856", "type-hash:0x2749c8a8",
                    "model-prefix-BM;exclude-common", "exefs-path-builder-and-parameter-table",
                ))
            db.executemany("INSERT INTO weapon_models VALUES(?,?,?,?)", weapon_model_rows)

            skill_tree_names = {
                int(row["SklTree_ID"]): row for row in read_csv(paths["skill_tree_names"])
                if int(row["SklTree_ID"]) >= 0
            }
            for skill_id, names in sorted(skill_tree_names.items()):
                db.execute("INSERT INTO skill_trees VALUES(?,?,?,?)", (
                    skill_id, names["SklTree_Name_1"], names["SklTree_Name_0"], names["SklTree_Name_3"],
                ))
            active_names = {
                int(row["Skl_ID"]): row for row in read_csv(paths["active_skill_names"])
                if int(row["Skl_ID"]) >= 0
            }
            for row in read_csv(paths["active_skills"]):
                skill_id = int(row["Skl_ID"])
                names = active_names[skill_id]
                tree_id = int(row["SklTree_ID"])
                if tree_id not in skill_tree_names:
                    raise ValueError(f"active skill {skill_id}: unknown tree {tree_id}")
                db.execute("INSERT INTO active_skills VALUES(?,?,?,?,?,?)", (
                    skill_id, tree_id, int(row["Pt"]), names["Skl_Name_1"],
                    names["Skl_Name_0"], names["Skl_Name_3"],
                ))

            for row in armor_set_rows:
                db.execute("INSERT INTO armor_sets VALUES(?,?,?,?,?,?,?,?,?,?)", (
                    row["set_id"], row["rank"], row["combat"], int(row["model_id"]),
                    row["name_cn"], row["name_en"], int(row["display_order"]),
                    row["review_status"], row["source"], row["notes"],
                ))
            for row in armor_rows:
                dex_id = int(row["Amr_ID"])
                member = member_by_dex[dex_id]
                names = armor_names[dex_id]
                part, save_type = ARMOR_PARTS[int(row["Part"])]
                save_id = int(member["save_id"]) if member["save_id"] else None
                db.execute("INSERT INTO armors VALUES(" + ",".join("?" for _ in range(21)) + ")", (
                    dex_id, save_type if save_id is not None else None, save_id,
                    part, ARMOR_COMBAT[int(row["BorG"])], ARMOR_GENDER[int(row["MorF"])],
                    names["Amr_Name_1"], names["Amr_Name_0"], names["Amr_Name_3"],
                    int(row["Rare"]), int(row["Slot"]), int(row["Def"]), int(row["MaxDef"]), int(row["Price"]),
                    int(row["Res_Fire"]), int(row["Res_Water"]), int(row["Res_Ice"]),
                    int(row["Res_Thunder"]), int(row["Res_Dragon"]), int(save_id is not None), member["mapping_source"],
                ))
            for member in armor_member_rows:
                db.execute("INSERT INTO armor_set_members VALUES(?,?,?,?)", (
                    member["set_id"], int(member["dex_id"]), member["gender"], int(member["slot_order"]),
                ))
            for row in sorted(armor_material_rows, key=lambda value: int(value["ID"])):
                db.execute("INSERT INTO armor_materials VALUES(?,?,?)", (
                    int(row["Amr_ID"]), int(row["Itm_ID"]), int(row["Qty"]),
                ))
            for row in sorted(read_csv(paths["armor_skill_points"]), key=lambda value: int(value["ID"])):
                db.execute("INSERT INTO armor_skill_points VALUES(?,?,?)", (
                    int(row["Amr_ID"]), int(row["SklTree_ID"]), int(row["Pt"]),
                ))
            for row in armor_model_rows:
                db.execute("INSERT INTO armor_model_resources VALUES(?,?,?,?,?,?)", (
                    row["model_key"], int(row["model_id"]), row["gender"], row["part"],
                    row["arc_relative_path"], row["mapping_source"],
                ))

            db.commit()
            integrity = db.execute("PRAGMA integrity_check").fetchone()[0]
            if integrity != "ok":
                raise ValueError(f"SQLite integrity check failed: {integrity}")
            db.execute("VACUUM")
        finally:
            db.close()

        stage.replace(output)

    file_hashes = {name: {"sha256": sha256(path), "bytes": path.stat().st_size} for name, path in sorted(paths.items())}
    manifest = {
        "format": "mh3g-encyclopedia-manifest-v2",
        "generator_version": VERSION,
        "source_files": file_hashes,
        "crosswalk_sha256": sha256(CROSSWALK),
        "model_crosswalk_sha256": sha256(MODEL_CROSSWALK),
        "database": {"file": output.name, "sha256": sha256(output), "bytes": output.stat().st_size},
        "counts": {
            "weapon_types": 12,
            "weapons": len(weapon_rows),
            "mapped_weapons": sum(value[1] is not None for value in resolved_weapons.values()),
            "items": len(used_item_ids),
            "mapped_items": sum(value[0] is not None for value in resolved_items.values()),
            "materials": len(material_rows),
            "model_resources": len({value[0] for value in model_by_save.values()}),
            "weapon_models": sum(value[1] is not None for value in resolved_weapons.values()),
            "armors": len(armor_rows),
            "mapped_armors": sum(bool(row["save_id"]) for row in armor_member_rows),
            "armor_sets": len(armor_set_rows),
            "armor_set_members": len(armor_member_rows),
            "armor_materials": len(armor_material_rows),
            "skill_trees": len([row for row in read_csv(paths["skill_tree_names"]) if int(row["SklTree_ID"]) >= 0]),
            "active_skills": len(read_csv(paths["active_skills"])),
            "armor_skill_points": len(read_csv(paths["armor_skill_points"])),
            "armor_model_resources": len(armor_model_rows),
        },
    }
    manifest_path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8", newline="\n")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dex-dump", required=True, type=Path)
    parser.add_argument("--data", type=Path, default=ROOT / "data")
    parser.add_argument("--output", type=Path, default=ROOT / "data" / "encyclopedia.sqlite")
    parser.add_argument("--manifest", type=Path, default=ROOT / "data" / "encyclopedia-manifest.json")
    args = parser.parse_args()
    build(args.dex_dump.resolve(), args.data.resolve(), args.output.resolve(), args.manifest.resolve())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
