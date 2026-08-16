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
