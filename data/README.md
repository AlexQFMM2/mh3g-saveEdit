# MH3G / MH3U data provenance

`cn/` and `en/` are generated lookup tables for the save editor. Do not
renumber a table after removing `DUMMY`, `None`, or unused entries: item,
weapon, and armor IDs are the original zero-based array positions used by the
game resources and the save format.

## Evidence and limits

- The authoritative source is `arc/ID/ID_res.arc` from the audited MH3G CCI.
  Its SHA-256 is
  `81e316bdfb0e65c1b05f1b375265aaff2cc3a1dc4c8cb5b8be23f0abd9b73087`.
- `tools/export_game_names.py` parses ARC v0x10 and the 18 GMD v1 arrays for
  1 item table, 5 armor parts, and 12 weapon classes. It also requires every
  paired item/weapon/armor description array to have the exact same count.
- Array positions determine IDs and table lengths. All generated authoritative
  CSV files cover the complete dense range `0..N-1`, including placeholders.
- This Chinese image uses replacement-font glyph mapping. Decoded GMD strings
  are therefore not automatically trusted as ordinary Unicode Chinese.
  Existing reviewed MH3U lists provide normal display names; Japan-only valid
  slots use the explicit `tools/mh3g_display_crosswalk.json` mapping.
- English names for Japan-only collaboration entries are descriptive
  translations and are not claimed to be official western MH3U names.

`manifest.json` records the generator version, ARC/GMD hashes, table counts,
CSV row counts, and CSV hashes. Raw CCI, RomFS, ARC, GMD, and save samples are
not part of this repository.

## Weapon and armor encyclopedia

`encyclopedia.sqlite` is a generated, read-only MH3G weapon database. The Dex
supplies attributes, the preorder upgrade tree and recipe relationships, but
its global `Wpn_ID` and `Itm_ID` values are never written to a save. The build
crosswalks every weapon and recipe material to the audited `ID_res.arc` arrays
and stores the result separately as `save_type/save_id`.

The committed v2 database contains 1,421 weapons, 1,651 armors, 331 explicit
armor sets, 5,806 weapon recipe rows, 6,398 armor recipe rows, 6,814 armor
skill-point rows and the 701 items referenced by those recipes. `encyclopedia-manifest.json` records every
raw input hash and the database hash. Raw Dex CSV files stay outside the
repository.

`model_resources` contains the 558 canonical ARC paths and format selectors;
`weapon_models` maps all 1,421 weapon rows to those shared models. The audited
crosswalk is `tools/mh3g_weapon_model_crosswalk.json`. It was extracted from
the decompressed ExeFS weapon parameter records (SHA-256
`5374eaac8de5395f346933c4523019a6f643b72e3a73778ccf9a2ac4c32aaa1d`), where
record offset zero is the model ID used by the game's resource-path builder.
The compact run-length representation is keyed by real save ID and is checked
against the SQLite rows on every Action build.

## Rebuild

On Windows, export the audited resource arrays to a temporary JSON file:

```powershell
python tools\export_game_names.py `
  "D:\MH\mh3G\cci_unpacked\romfs\arc\ID\ID_res.arc" `
  "$env:TEMP\mh3g-game-names.json" `
  --language cn-font-remap
```

Generate and validate on Linux (the existing `data/` directory supplies the
reviewed display-name reference):

```bash
python3 tools/build_data.py \
  --game-names /tmp/mh3g-game-names.json \
  --reference-data data \
  --output data
python3 tools/validate_data.py data \
  --game-names /tmp/mh3g-game-names.json \
  --sample ../research/mh3u/samples/3ds/user1 \
  --sample ../research/mh3u/samples/wiiu/80000002/user1
```

For a determinism check, generate twice into two temporary directories and
compare them with `diff -qr`.

Generate the weapon encyclopedia from the external Dex dump:

```bash
python3 tools/build_encyclopedia.py --dex-dump /tmp/mh3g-dex
python3 tools/validate_encyclopedia.py data
python3 tools/validate_model_crosswalk.py
```

The required dump files and their exact hashes are listed in
`encyclopedia-manifest.json`. Any exact-name mismatch must be reviewed in
`tools/mh3g_encyclopedia_crosswalk.json`; the generator does not use fuzzy
matching or positional offsets.

## Armor-set crosswalk

The armor encyclopedia uses the committed `tools/mh3g_armor_sets.csv` and
`tools/mh3g_armor_set_members.csv` as its authoritative grouping. Runtime code
must not infer sets from a shared model number, rarity, or name prefix. Those
signals are used only by `tools/suggest_armor_sets.py` to create a first-pass
scaffold. See `tools/MH3G_ARMOR_SETS.md` for the editing workflow and run
`python3 tools/validate_armor_sets.py` after every manual adjustment.
