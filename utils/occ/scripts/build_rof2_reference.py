#!/usr/bin/env python3

from __future__ import annotations

import csv
import json
import re
from pathlib import Path
from urllib.request import urlopen

SHEET_URL = "https://docs.google.com/spreadsheets/d/1ZejpzNJBSVkSeisqiFd2ZyqvNl52XHe79ZlhnZ9UxBk/export?format=csv&gid=984146946"
PATCH_PATH = Path(r"C:\AkkStack\code\utils\patches\patch_RoF2.conf")
OUTPUT_PATH = Path(r"C:\AkkStack\code\utils\occ\data\rof2-reference.json")
PATCH_RE = re.compile(r"^(OP_[A-Za-z0-9_]+)=0x([0-9A-Fa-f]{4})")


def normalize_opcode(value: str) -> str:
    raw = (value or "").strip().lower()
    if not raw:
        return ""
    if raw.startswith("0x"):
        raw = raw[2:]
    if not re.fullmatch(r"[0-9a-f]{1,4}", raw):
        return ""
    return f"0x{int(raw, 16):04x}"


def trim(value: str) -> str:
    return (value or "").strip()


def load_patch_map(path: Path) -> dict[str, str]:
    mapping: dict[str, str] = {}
    for raw_line in path.read_text(encoding="utf-8", errors="ignore").splitlines():
        match = PATCH_RE.match(raw_line.strip())
        if match:
            mapping[f"0x{match.group(2).lower()}"] = match.group(1)
    return mapping


def fetch_sheet_rows(url: str) -> list[list[str]]:
    with urlopen(url) as response:
        text = response.read().decode("utf-8-sig", errors="ignore")
    return list(csv.reader(text.splitlines()))


def build_entries(rows: list[list[str]], patch_map: dict[str, str]) -> list[dict]:
    entries: list[dict] = []
    seen_opcodes: set[str] = set()

    for row_index, row in enumerate(rows[1:], start=2):
        padded = row + [""] * (10 - len(row))
        larion_opcode, larion_name, live_opcode, live_name, notes, test_opcode, _, rof2_opcode, rof2_name, rof2_extra = padded[:10]
        code = normalize_opcode(rof2_opcode)
        name = trim(rof2_name)
        eqemu_name = patch_map.get(code, "") if code else ""
        note_text = trim(notes)
        extra_text = trim(rof2_extra)

        if not any((code, name, eqemu_name, note_text, extra_text)):
            continue

        if name and eqemu_name:
            alignment = "match" if name == eqemu_name else "mismatch"
        elif name:
            alignment = "sheet-only"
        elif eqemu_name:
            alignment = "patch-only-name"
        else:
            alignment = "unnamed"

        entries.append(
            {
                "id": f"sheet-{row_index}",
                "sheet_row": row_index,
                "source_type": "sheet+patch" if code and eqemu_name else "sheet",
                "rof2_opcode": code,
                "rof2_name": name,
                "eqemu_name": eqemu_name,
                "display_name": name or eqemu_name or code,
                "alignment": alignment,
                "notes": note_text,
                "extra": extra_text,
                "larion_opcode": normalize_opcode(larion_opcode),
                "larion_name": trim(larion_name),
                "live_opcode": normalize_opcode(live_opcode),
                "live_name": trim(live_name),
                "test_opcode": normalize_opcode(test_opcode),
            }
        )

        if code:
            seen_opcodes.add(code)

    for opcode, eqemu_name in sorted(patch_map.items(), key=lambda item: int(item[0], 16)):
        if opcode in seen_opcodes:
            continue
        entries.append(
            {
                "id": f"patch-{opcode}",
                "sheet_row": None,
                "source_type": "patch-only",
                "rof2_opcode": opcode,
                "rof2_name": "",
                "eqemu_name": eqemu_name,
                "display_name": eqemu_name,
                "alignment": "patch-only",
                "notes": "",
                "extra": "",
                "larion_opcode": "",
                "larion_name": "",
                "live_opcode": "",
                "live_name": "",
                "test_opcode": "",
            }
        )

    return entries


def main() -> int:
    rows = fetch_sheet_rows(SHEET_URL)
    patch_map = load_patch_map(PATCH_PATH)
    entries = build_entries(rows, patch_map)
    payload = {
        "title": "Opcode Command Center",
        "client": "RoF2",
        "source_sheet": SHEET_URL,
        "sheet_gid": 984146946,
        "entry_count": len(entries),
        "entries": entries,
    }
    OUTPUT_PATH.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT_PATH.write_text(json.dumps(payload, indent=2), encoding="utf-8")
    print(f"Wrote {len(entries)} entries to {OUTPUT_PATH}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
