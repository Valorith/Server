#!/usr/bin/env python3

from __future__ import annotations

import argparse
import csv
import json
import subprocess
import sys
from pathlib import Path

CLIENT_PATCHES = {
    "titanium": "patch_Titanium.conf",
    "sof": "patch_SoF.conf",
    "sod": "patch_SoD.conf",
    "uf": "patch_UF.conf",
    "rof": "patch_RoF.conf",
    "rof2": "patch_RoF2.conf",
}

CLIENT_ALIASES = {
    "tit": "titanium",
    "titanium": "titanium",
    "sof": "sof",
    "sod": "sod",
    "uf": "uf",
    "rof": "rof",
    "rof2": "rof2",
}


def normalize_client(value: str) -> str:
    key = value.strip().lower()
    if key not in CLIENT_ALIASES:
        raise ValueError(f"Unsupported client '{value}'. Expected Titanium, SoF, SoD, UF, RoF, or RoF2.")
    return CLIENT_ALIASES[key]


def looks_like_repo_root(path: Path) -> bool:
    return (path / "utils" / "patches").is_dir() and (path / "common" / "eq_packet.cpp").is_file()


def find_repo_root(explicit: str | None) -> Path:
    candidates: list[Path] = []
    if explicit:
        candidates.append(Path(explicit).expanduser().resolve())

    cwd = Path.cwd().resolve()
    candidates.extend([cwd, *cwd.parents, Path(r"C:\AkkStack\code")])

    seen: set[Path] = set()
    for candidate in candidates:
        if candidate in seen:
            continue
        seen.add(candidate)
        if looks_like_repo_root(candidate):
            return candidate
    raise FileNotFoundError("Could not locate EQEmu repo root.")


def load_patch_map(repo_root: Path, client_key: str) -> dict[int, list[str]]:
    patch_path = repo_root / "utils" / "patches" / CLIENT_PATCHES[client_key]
    if not patch_path.is_file():
        raise FileNotFoundError(f"Patch file not found: {patch_path}")

    mapping: dict[int, list[str]] = {}
    for raw_line in patch_path.read_text(encoding="utf-8", errors="ignore").splitlines():
        line = raw_line.strip()
        if not line.startswith("OP_") or "=" not in line:
            continue
        name, value_text = line.split("=", 1)
        try:
            value = int(value_text, 16)
        except ValueError:
            continue
        mapping.setdefault(value, []).append(name)
    return mapping


def load_session(session_name: str) -> tuple[dict, list[dict]]:
    session_root = Path(r"C:\AkkStack\.codex\captures\sessions") / session_name
    meta_path = session_root / "session.json"
    markers_path = session_root / "markers.jsonl"

    if not meta_path.is_file():
        raise FileNotFoundError(f"Session metadata not found: {meta_path}")

    meta = json.loads(meta_path.read_text(encoding="utf-8-sig"))
    markers: list[dict] = []
    if markers_path.is_file():
        for line in markers_path.read_text(encoding="utf-8-sig", errors="ignore").splitlines():
            line = line.strip()
            if not line:
                continue
            markers.append(json.loads(line))
    return meta, markers


def pick_marker(markers: list[dict], marker_name: str) -> dict:
    if not markers:
        raise ValueError("No markers found in this session.")
    if marker_name == "latest":
        return markers[-1]
    for marker in reversed(markers):
        if marker.get("Label") == marker_name:
            return marker
    raise ValueError(f"Marker '{marker_name}' was not found.")


def run_tshark_export(tshark_path: str, capture_path: str, start_epoch: float, end_epoch: float) -> list[dict]:
    fields = [
        "frame.number",
        "frame.time_epoch",
        "ip.src",
        "udp.srcport",
        "ip.dst",
        "udp.dstport",
        "frame.len",
        "_ws.col.info",
        "udp.payload",
        "data.data",
    ]
    display_filter = f"udp && frame.time_epoch >= {start_epoch:.6f} && frame.time_epoch <= {end_epoch:.6f}"
    command = [
        tshark_path,
        "-r",
        capture_path,
        "-n",
        "-Y",
        display_filter,
        "-T",
        "fields",
        "-E",
        "header=y",
        "-E",
        "separator=\t",
        "-E",
        "quote=n",
        "-E",
        "occurrence=f",
    ]
    for field in fields:
        command.extend(["-e", field])

    completed = subprocess.run(command, capture_output=True, text=True, check=False)
    if completed.returncode != 0:
        raise RuntimeError(completed.stderr.strip() or f"tshark exited with code {completed.returncode}")

    lines = [line for line in completed.stdout.splitlines() if line.strip()]
    if not lines:
        return []

    reader = csv.DictReader(lines, delimiter="\t")
    return list(reader)


def payload_from_row(row: dict) -> str:
    return (row.get("udp.payload") or row.get("data.data") or "").replace(":", "").strip().lower()


def opcode_candidates(payload_hex: str, mapping: dict[int, list[str]], max_offset: int = 8) -> list[dict]:
    if len(payload_hex) < 4:
        return []

    candidates: list[dict] = []
    max_bytes = min(max_offset, max(0, len(payload_hex) // 2 - 2))
    for offset in range(max_bytes + 1):
        start = offset * 2
        pair = payload_hex[start:start + 4]
        if len(pair) < 4:
            continue
        low = int(pair[0:2], 16)
        high = int(pair[2:4], 16)
        opcode = low | (high << 8)
        names = mapping.get(opcode, [])
        names = [name for name in names if name != "OP_Unknown"]
        if names:
            candidates.append(
                {
                    "offset": offset,
                    "opcode": f"0x{opcode:04x}",
                    "names": names,
                }
            )
    return candidates


def classify_packets(rows: list[dict], marker_epoch: float, pre_window: float, post_window: float, mapping: dict[int, list[str]]) -> tuple[list[dict], list[dict], list[dict]]:
    pre_packets: list[dict] = []
    post_packets: list[dict] = []
    around_packets: list[dict] = []

    for row in rows:
        try:
            ts = float(row["frame.time_epoch"])
        except (KeyError, TypeError, ValueError):
            continue
        packet = {
            "frame": row.get("frame.number", ""),
            "time_epoch": ts,
            "delta": ts - marker_epoch,
            "src": row.get("ip.src", ""),
            "srcport": row.get("udp.srcport", ""),
            "dst": row.get("ip.dst", ""),
            "dstport": row.get("udp.dstport", ""),
            "length": row.get("frame.len", ""),
            "info": row.get("_ws.col.info", ""),
            "payload": payload_from_row(row),
        }
        packet["signature"] = f"{packet['src']}:{packet['srcport']}->{packet['dst']}:{packet['dstport']}|{packet['length']}|{packet['payload'][:24]}"
        packet["opcode_candidates"] = opcode_candidates(packet["payload"], mapping)
        packet["side"] = "before" if packet["delta"] < 0 else "after"

        if marker_epoch - pre_window <= ts < marker_epoch:
            pre_packets.append(packet)
            around_packets.append(packet)
        elif marker_epoch <= ts <= marker_epoch + post_window:
            post_packets.append(packet)
            around_packets.append(packet)

    return pre_packets, post_packets, around_packets


def rank_packets(pre_packets: list[dict], post_packets: list[dict], around_packets: list[dict]) -> list[dict]:
    pre_signatures = {packet["signature"] for packet in pre_packets}

    for packet in around_packets:
        packet["is_new_vs_pre"] = packet["signature"] not in pre_signatures
        offset_score = min((candidate["offset"] for candidate in packet["opcode_candidates"]), default=99)
        packet["rank"] = (
            abs(packet["delta"]),
            0 if packet["opcode_candidates"] else 1,
            0 if packet["is_new_vs_pre"] else 1,
            offset_score,
        )

    return sorted(around_packets, key=lambda packet: packet["rank"])


def print_report(meta: dict, marker: dict, ranked_packets: list[dict], pre_packets: list[dict], post_packets: list[dict], limit: int) -> None:
    print(f"Session: {meta.get('SessionName', '')}")
    print(f"Capture: {meta.get('CapturePath', '')}")
    print(f"Marker: {marker.get('Label', '')}")
    print(f"Marked UTC: {marker.get('MarkedUtc', '')}")
    print(f"Pre-window packets: {len(pre_packets)}")
    print(f"Post-window packets: {len(post_packets)}")
    print()
    print("Top packets near marker:")

    for packet in ranked_packets[:limit]:
        print(
            f"  delta={packet['delta']:+.3f}s frame={packet['frame']} "
            f"{packet['src']}:{packet['srcport']} -> {packet['dst']}:{packet['dstport']} len={packet['length']} "
            f"side={packet['side']} new_vs_pre={packet['is_new_vs_pre']}"
        )
        if packet["payload"]:
            print(f"    payload_prefix={packet['payload'][:48]}")
        if packet["opcode_candidates"]:
            formatted = "; ".join(
                f"offset {candidate['offset']}: {candidate['opcode']} => {', '.join(candidate['names'])}"
                for candidate in packet["opcode_candidates"]
            )
            print(f"    opcode_candidates={formatted}")
        if packet["info"]:
            print(f"    info={packet['info']}")

    if not ranked_packets:
        print("  No UDP packets were found in the selected marker window.")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Analyze packets around a tshark session marker and surface likely opcode candidates.")
    parser.add_argument("--session-name", required=True)
    parser.add_argument("--client", required=True)
    parser.add_argument("--marker", default="latest", help="Marker label or 'latest'.")
    parser.add_argument("--pre-window", type=float, default=1.0)
    parser.add_argument("--post-window", type=float, default=2.0)
    parser.add_argument("--limit", type=int, default=8)
    parser.add_argument("--tshark-path", default=r"C:\Program Files\Wireshark\tshark.exe")
    parser.add_argument("--repo-root")
    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()

    try:
        repo_root = find_repo_root(args.repo_root)
        client_key = normalize_client(args.client)
        mapping = load_patch_map(repo_root, client_key)
        meta, markers = load_session(args.session_name)
        marker = pick_marker(markers, args.marker)
        marker_epoch = float(marker["MarkedEpoch"])
        rows = run_tshark_export(
            args.tshark_path,
            meta["CapturePath"],
            marker_epoch - args.pre_window,
            marker_epoch + args.post_window,
        )
        pre_packets, post_packets, around_packets = classify_packets(rows, marker_epoch, args.pre_window, args.post_window, mapping)
        ranked_packets = rank_packets(pre_packets, post_packets, around_packets)
        print_report(meta, marker, ranked_packets, pre_packets, post_packets, args.limit)
        return 0
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
