#!/usr/bin/env python3
"""Export authoritative save-ID arrays from MH3G ID_res.arc.

The Chinese MH3G image uses a replacement font, so some decoded Unicode text
does not look like the text rendered by the game.  The strings are nevertheless
exported verbatim: their array positions and table lengths are authoritative;
the strings themselves are evidence/crosswalk material, not automatically
trusted Chinese display names.

The output contains decoded strings and source hashes only.  CCI, RomFS, ARC,
and GMD files stay outside the repository.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
import zlib
from pathlib import Path


FORMAT = "mh3g-game-name-export-v1"
ARC_MAGIC = b"ARC\0"
GMD_MAGIC = b"GMD\0"
ARC_HEADER_SIZE = 12
ARC_ENTRY_SIZE = 80
ARC_NAME_SIZE = 64

AUTHORITATIVE_TABLES = {
    "GUI\\font\\Item00_jpn": "items",
    "GUI\\font\\Helm_jpn": "head_armors",
    "GUI\\font\\Body_jpn": "chest_armors",
    "GUI\\font\\Arm_jpn": "arms_armors",
    "GUI\\font\\Waist_jpn": "waist_armors",
    "GUI\\font\\Leg_jpn": "legs_armors",
    "GUI\\font\\Lsword_jpn": "gs_weapons",
    "GUI\\font\\Sword_jpn": "sns_weapons",
    "GUI\\font\\Hammer_jpn": "h_weapons",
    "GUI\\font\\Lsword2_jpn": "ls_weapons",
    "GUI\\font\\Axe_jpn": "sa_weapons",
    "GUI\\font\\Lance_jpn": "l_weapons",
    "GUI\\font\\Hbg_jpn": "hbg_weapons",
    "GUI\\font\\Lbg_jpn": "lbg_weapons",
    "GUI\\font\\Gunlance_jpn": "gl_weapons",
    "GUI\\font\\WSword_jpn": "db_weapons",
    "GUI\\font\\Bow_jpn": "bow_weapons",
    "GUI\\font\\Pipe_jpn": "hh_weapons",
}

PAIRED_TABLES = {
    "GUI\\font\\ItemDetail_jpn": "items",
    "GUI\\font\\Helm_Exp_jpn": "head_armors",
    "GUI\\font\\Body_Exp_jpn": "chest_armors",
    "GUI\\font\\Arm_Exp_jpn": "arms_armors",
    "GUI\\font\\Waist_Exp_jpn": "waist_armors",
    "GUI\\font\\Leg_Exp_jpn": "legs_armors",
    "GUI\\font\\Lsword_Exp_jpn": "gs_weapons",
    "GUI\\font\\Sword_Exp_jpn": "sns_weapons",
    "GUI\\font\\Hammer_Exp_jpn": "h_weapons",
    "GUI\\font\\Lsword2_Exp_jpn": "ls_weapons",
    "GUI\\font\\Axe_Exp_jpn": "sa_weapons",
    "GUI\\font\\Lance_Exp_jpn": "l_weapons",
    "GUI\\font\\Hbg_Exp_jpn": "hbg_weapons",
    "GUI\\font\\Lbg_Exp_jpn": "lbg_weapons",
    "GUI\\font\\Gunlance_Exp_jpn": "gl_weapons",
    "GUI\\font\\WSword_Exp_jpn": "db_weapons",
    "GUI\\font\\Bow_Exp_jpn": "bow_weapons",
    "GUI\\font\\Pipe_Exp_jpn": "hh_weapons",
}
REQUIRED_ARC_ENTRIES = set(AUTHORITATIVE_TABLES) | set(PAIRED_TABLES)


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def read_arc(path: Path) -> tuple[int, dict[str, bytes], dict[str, dict[str, object]]]:
    data = path.read_bytes()
    if len(data) < ARC_HEADER_SIZE or data[:4] != ARC_MAGIC:
        raise ValueError(f"{path}: not an MT Framework ARC archive")
    version, count = struct.unpack_from("<HH", data, 4)
    if version != 0x10:
        raise ValueError(f"{path}: unsupported ARC version 0x{version:04x}")
    table_end = ARC_HEADER_SIZE + count * ARC_ENTRY_SIZE
    if table_end > len(data):
        raise ValueError(f"{path}: truncated ARC entry table")

    payloads: dict[str, bytes] = {}
    metadata: dict[str, dict[str, object]] = {}
    for index in range(count):
        entry = ARC_HEADER_SIZE + index * ARC_ENTRY_SIZE
        raw_name = data[entry:entry + ARC_NAME_SIZE].split(b"\0", 1)[0]
        name = raw_name.decode("ascii", errors="strict")
        if name not in REQUIRED_ARC_ENTRIES:
            continue
        if name in payloads:
            raise ValueError(f"{path}: duplicate authoritative ARC entry {name}")

        compressed_size, size_flags, offset = struct.unpack_from(
            "<III", data, entry + ARC_NAME_SIZE + 4
        )
        expected_size = size_flags & 0x00FFFFFF
        end = offset + compressed_size
        if offset < table_end or end > len(data):
            raise ValueError(f"{path}: {name} payload is outside the archive")
        try:
            payload = zlib.decompress(data[offset:end])
        except zlib.error as exc:
            raise ValueError(f"{path}: cannot decompress {name}: {exc}") from exc
        if len(payload) != expected_size:
            raise ValueError(
                f"{path}: {name} size mismatch: expected {expected_size}, got {len(payload)}"
            )
        payloads[name] = payload
        metadata[name] = {
            "arc_index": index,
            "compressed_size": compressed_size,
            "size": len(payload),
            "sha256": sha256_bytes(payload),
        }
    return count, payloads, metadata


def read_gmd(data: bytes, label: str) -> list[str]:
    if len(data) < 32 or data[:4] != GMD_MAGIC:
        raise ValueError(f"{label}: not a GMD message table")
    message_count, label_size, string_size, filename_length = struct.unpack_from(
        "<IIII", data, 0x10
    )
    filename_end = 0x20 + filename_length
    if filename_end >= len(data) or data[filename_end] != 0:
        raise ValueError(f"{label}: invalid GMD filename field")

    # GMD v1 stores two 32-bit values after the NUL-terminated filename,
    # followed by the label section and then the packed NUL-terminated strings.
    strings_offset = filename_end + 1 + 8 + label_size
    strings_end = strings_offset + string_size
    if strings_end != len(data):
        raise ValueError(
            f"{label}: string section mismatch: offset={strings_offset}, "
            f"size={string_size}, file={len(data)}"
        )
    raw_strings = data[strings_offset:strings_end].split(b"\0")
    if not raw_strings or raw_strings[-1] != b"":
        raise ValueError(f"{label}: string section is not NUL terminated")
    raw_strings.pop()
    if len(raw_strings) != message_count:
        raise ValueError(
            f"{label}: expected {message_count} messages, decoded {len(raw_strings)}"
        )
    try:
        return [value.decode("utf-8", errors="strict") for value in raw_strings]
    except UnicodeDecodeError as exc:
        raise ValueError(f"{label}: message text is not valid UTF-8: {exc}") from exc


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("id_res", type=Path, help="extracted MH3G arc/ID/ID_res.arc")
    parser.add_argument("output", type=Path, help="output JSON outside the repository")
    parser.add_argument(
        "--language",
        default="cn-font-remap",
        choices=("cn-font-remap", "en", "jp"),
        help="language/resource variant contained in the *_jpn entries",
    )
    args = parser.parse_args()

    source = args.id_res.resolve()
    entry_count, archive, entry_metadata = read_arc(source)
    missing = REQUIRED_ARC_ENTRIES - set(archive)
    if missing:
        raise ValueError(f"ID_res.arc is missing required MH3G name tables: {sorted(missing)}")

    tables: dict[str, list[str]] = {}
    resources: dict[str, dict[str, object]] = {}
    for arc_name, output_name in sorted(AUTHORITATIVE_TABLES.items(), key=lambda item: item[1]):
        tables[output_name] = read_gmd(archive[arc_name], arc_name)
        resources[output_name] = {
            "arc_path": arc_name,
            "messages": len(tables[output_name]),
            **entry_metadata[arc_name],
        }

    paired_resources: dict[str, dict[str, object]] = {}
    for arc_name, output_name in sorted(PAIRED_TABLES.items(), key=lambda item: item[1]):
        messages = read_gmd(archive[arc_name], arc_name)
        if len(messages) != len(tables[output_name]):
            raise ValueError(
                f"{arc_name}: {len(messages)} descriptions do not match "
                f"{len(tables[output_name])} {output_name} names"
            )
        paired_resources[output_name] = {
            "arc_path": arc_name,
            "messages": len(messages),
            **entry_metadata[arc_name],
        }

    payload = {
        "format": FORMAT,
        "language": args.language,
        "font_remap_warning": args.language == "cn-font-remap",
        "source": {
            "file": source.name,
            "size": source.stat().st_size,
            "sha256": sha256(source),
            "arc_version": "0x0010",
            "arc_entry_count": entry_count,
        },
        "resources": resources,
        "paired_resources": paired_resources,
        "tables": tables,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        json.dumps(payload, ensure_ascii=False, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
        newline="\n",
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
