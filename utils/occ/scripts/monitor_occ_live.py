#!/usr/bin/env python3

from __future__ import annotations

import argparse
import csv
import json
import subprocess
import sys
import time
import zlib
from datetime import datetime, timezone
from pathlib import Path

from occ_runtime import find_tshark_path as resolve_runtime_tshark_path, run_command

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

IGNORED_UDP_PORTS = {137, 138, 1900, 5353, 5355, 21027}
LIKELY_EQ_PORTS = {5998, 5999, 7778}
REPO_ROOT_HINT = Path(__file__).resolve().parents[3]
CAPTURES_ROOT_HINT = REPO_ROOT_HINT.parent / ".codex" / "captures" / "sessions"


def normalize_client(value: str) -> str:
    key = value.strip().lower()
    if key not in CLIENT_ALIASES:
        raise ValueError(f"Unsupported client '{value}'. Expected Titanium, SoF, SoD, UF, RoF, or RoF2.")
    return CLIENT_ALIASES[key]


def display_client(client_key: str) -> str:
    return {
        "titanium": "Titanium",
        "sof": "SoF",
        "sod": "SoD",
        "uf": "UF",
        "rof": "RoF",
        "rof2": "RoF2",
    }[client_key]


def looks_like_repo_root(path: Path) -> bool:
    return (path / "utils" / "patches").is_dir() and (path / "common" / "eq_packet.cpp").is_file()


def find_repo_root(explicit: str | None) -> Path:
    candidates: list[Path] = []
    if explicit:
        candidates.append(Path(explicit).expanduser().resolve())

    cwd = Path.cwd().resolve()
    candidates.extend([cwd, *cwd.parents, REPO_ROOT_HINT])

    seen: set[Path] = set()
    for candidate in candidates:
        if candidate in seen:
            continue
        seen.add(candidate)
        if looks_like_repo_root(candidate):
            return candidate
    raise FileNotFoundError("Could not locate EQEmu repo root.")


def find_tshark_path(explicit: str | None) -> Path:
    resolved = resolve_runtime_tshark_path(explicit)
    if resolved:
        return resolved

    if sys.platform == "darwin" or sys.platform.startswith("linux"):
        raise FileNotFoundError("tshark was not found. Run utils/occ/start_occ.sh for guided setup.")
    raise FileNotFoundError("tshark.exe was not found. Run utils\\occ\\start_occ.ps1 for guided setup.")


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


def session_paths(session_name: str, captures_root: Path | None = None) -> dict[str, Path]:
    root = (captures_root or CAPTURES_ROOT_HINT) / session_name
    return {
        "root": root,
        "meta": root / "session.json",
        "capture": root / f"{session_name}.pcapng",
        "detections": root / "detections.json",
    }


def load_json(path: Path) -> dict:
    if not path.is_file():
        return {}
    return json.loads(path.read_text(encoding="utf-8-sig"))


def write_json(path: Path, payload: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2), encoding="utf-8")


def wait_for_capture(path: Path, timeout_seconds: float) -> bool:
    deadline = time.time() + timeout_seconds
    while time.time() < deadline:
        if path.is_file():
            return True
        time.sleep(0.25)
    return path.is_file()


def payload_from_row(row: dict) -> str:
    return (row.get("udp.payload") or row.get("data.data") or "").replace(":", "").strip().lower()


def build_analysis_views(payload_hex: str) -> list[dict]:
    if len(payload_hex) < 4:
        return []

    try:
        payload_bytes = bytes.fromhex(payload_hex)
    except ValueError:
        return []

    views = [
        {
            "source": "raw",
            "offsetBase": 0,
            "payloadHex": payload_hex,
        }
    ]

    for offset in range(0, min(6, len(payload_bytes))):
        try:
            decompressed = zlib.decompress(payload_bytes[offset:])
        except Exception:
            continue
        if len(decompressed) < 2:
            continue
        views.append(
            {
                "source": f"zlib@{offset}",
                "offsetBase": offset,
                "payloadHex": decompressed.hex(),
            }
        )

    unique_views: list[dict] = []
    seen: set[tuple[str, str]] = set()
    for view in views:
        key = (view["source"], view["payloadHex"])
        if key in seen:
            continue
        seen.add(key)
        unique_views.append(view)
    return unique_views


def opcode_candidates(payload_hex: str, mapping: dict[int, list[str]], max_offset: int = 8) -> list[dict]:
    if len(payload_hex) < 4:
        return []

    candidates: list[dict] = []
    for view in build_analysis_views(payload_hex):
        view_hex = view["payloadHex"]
        max_bytes = min(max_offset, max(0, len(view_hex) // 2 - 2))
        for offset in range(max_bytes + 1):
            start = offset * 2
            pair = view_hex[start:start + 4]
            if len(pair) < 4:
                continue
            low = int(pair[0:2], 16)
            high = int(pair[2:4], 16)
            opcode = low | (high << 8)
            if opcode == 0:
                continue
            names = [name for name in mapping.get(opcode, []) if name != "OP_Unknown"]
            if names:
                candidates.append(
                    {
                        "offset": offset,
                        "opcode": f"0x{opcode:04x}",
                        "names": names,
                        "source": view["source"],
                    }
                )
    return candidates


def is_likely_eq_port(port: int) -> bool:
    return port in LIKELY_EQ_PORTS or 7000 <= port <= 7999


def flow_key(src: str, src_port: int, dst: str, dst_port: int) -> tuple[tuple[str, int], tuple[str, int]]:
    left = (src or "", int(src_port or 0))
    right = (dst or "", int(dst_port or 0))
    return tuple(sorted((left, right)))


def classify_eq_like(src_port: int, dst_port: int, info: str, candidates: list[dict], ignored: bool) -> tuple[bool, str]:
    if ignored:
        return False, ""

    if is_likely_eq_port(src_port) or is_likely_eq_port(dst_port):
        matched_port = src_port if is_likely_eq_port(src_port) else dst_port
        return True, f"likely-eq-port:{matched_port}"

    if any((candidate.get("source") or "").startswith("zlib@") for candidate in candidates):
        return True, "compressed-opcode"

    info_upper = (info or "").upper()
    if "ABORT" in info_upper:
        return True, "abort-pattern"

    return False, ""


def derive_trusted_flows(activity: list[dict]) -> set[tuple[tuple[str, int], tuple[str, int]]]:
    stats: dict[tuple[tuple[str, int], tuple[str, int]], dict[str, int]] = {}
    for item in activity:
        key = item["flowKey"]
        bucket = stats.setdefault(
            key,
            {
                "count": 0,
                "eqPortHits": 0,
                "compressedHits": 0,
                "candidateHits": 0,
                "abortHits": 0,
            },
        )
        bucket["count"] += 1
        if is_likely_eq_port(item["srcPortNumber"]) or is_likely_eq_port(item["dstPortNumber"]):
            bucket["eqPortHits"] += 1
        if any((candidate.get("source") or "").startswith("zlib@") for candidate in item["candidates"]):
            bucket["compressedHits"] += 1
        if item["hasCandidates"]:
            bucket["candidateHits"] += 1
        if "ABORT" in (item["info"] or "").upper():
            bucket["abortHits"] += 1

    trusted: set[tuple[tuple[str, int], tuple[str, int]]] = set()
    for key, bucket in stats.items():
        if bucket["eqPortHits"] > 0:
            trusted.add(key)
            continue
        if bucket["compressedHits"] >= 2:
            trusted.add(key)
            continue
        if bucket["candidateHits"] >= 4 and bucket["abortHits"] >= 2:
            trusted.add(key)
            continue

    return trusted


def derive_eq_confidence(item: dict, trusted_flows: set[tuple[tuple[str, int], tuple[str, int]]]) -> tuple[str, str]:
    if item["ignored"]:
        return "none", ""

    in_trusted_flow = item["flowKey"] in trusted_flows
    has_compressed = any((candidate.get("source") or "").startswith("zlib@") for candidate in item["candidates"])
    has_eq_port = is_likely_eq_port(item["srcPortNumber"]) or is_likely_eq_port(item["dstPortNumber"])
    has_abort = "ABORT" in (item["info"] or "").upper()

    if in_trusted_flow and (has_eq_port or has_compressed or has_abort or item["hasCandidates"]):
        if has_eq_port:
            matched_port = item["srcPortNumber"] if is_likely_eq_port(item["srcPortNumber"]) else item["dstPortNumber"]
            return "high", f"trusted-eq-port:{matched_port}"
        if has_compressed:
            return "high", "trusted-compressed-flow"
        if has_abort:
            return "high", "trusted-abort-flow"
        return "medium", "trusted-session-flow"

    if item["hasCandidates"]:
        if has_compressed:
            return "medium", "compressed-opcode"
        return "low", "raw-candidate-only"

    if has_abort:
        return "low", "abort-without-trust"

    return "none", ""


def run_tshark_export(tshark_path: Path, capture_path: str, last_frame: int) -> list[dict]:
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
    display_filter = f"udp && frame.number > {last_frame}"
    command = [
        str(tshark_path),
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

    completed = run_command(command, capture_output=True, text=True, check=False)
    if completed.returncode != 0:
        stderr = (completed.stderr or "").strip().lower()
        if "appears to have been cut short" in stderr or "file has 0-byte packet" in stderr:
            return []
        raise RuntimeError(completed.stderr.strip() or f"tshark exited with code {completed.returncode}")

    lines = [line for line in completed.stdout.splitlines() if line.strip()]
    if not lines:
        return []

    reader = csv.DictReader(lines, delimiter="\t")
    return list(reader)


def iso_from_epoch(epoch: float) -> str:
    return datetime.fromtimestamp(epoch, tz=timezone.utc).isoformat()


def build_activity_and_detections(rows: list[dict], mapping: dict[int, list[str]]) -> tuple[list[dict], list[dict], int]:
    provisional_activity: list[dict] = []
    highest_frame = 0

    for row in rows:
        try:
            frame_number = int(row.get("frame.number", "0"))
            time_epoch = float(row.get("frame.time_epoch", "0"))
        except ValueError:
            continue

        try:
            src_port = int(row.get("udp.srcport", "0") or "0")
            dst_port = int(row.get("udp.dstport", "0") or "0")
        except ValueError:
            src_port = 0
            dst_port = 0

        highest_frame = max(highest_frame, frame_number)
        ignored = src_port in IGNORED_UDP_PORTS or dst_port in IGNORED_UDP_PORTS
        payload_hex = payload_from_row(row)
        candidates = [] if ignored else opcode_candidates(payload_hex, mapping)
        primary = min(
            candidates,
            key=lambda candidate: (0 if candidate.get("source", "").startswith("zlib@") else 1, candidate["offset"]),
        ) if candidates else None
        provisional_activity.append(
            {
                "id": f"frame-{frame_number}",
                "frameNumber": frame_number,
                "timeEpoch": time_epoch,
                "detectedUtc": iso_from_epoch(time_epoch),
                "src": row.get("ip.src", ""),
                "srcport": row.get("udp.srcport", ""),
                "srcPortNumber": src_port,
                "dst": row.get("ip.dst", ""),
                "dstport": row.get("udp.dstport", ""),
                "dstPortNumber": dst_port,
                "length": row.get("frame.len", ""),
                "info": row.get("_ws.col.info", ""),
                "payloadPrefix": payload_hex[:64],
                "opcode": primary["opcode"] if primary else "",
                "names": primary["names"] if primary else [],
                "analysisSource": primary["source"] if primary else "",
                "candidateCount": len(candidates),
                "hasCandidates": bool(candidates),
                "candidates": candidates,
                "ignored": ignored,
                "ignoredReason": f"known-noise-port:{src_port if src_port in IGNORED_UDP_PORTS else dst_port}" if ignored else "",
                "flowKey": flow_key(row.get("ip.src", ""), src_port, row.get("ip.dst", ""), dst_port),
            }
        )

    trusted_flows = derive_trusted_flows(provisional_activity)
    activity: list[dict] = []
    detections: list[dict] = []

    for item in provisional_activity:
        eq_like, eq_like_reason = classify_eq_like(
            item["srcPortNumber"], item["dstPortNumber"], item["info"], item["candidates"], item["ignored"]
        )
        eq_confidence, eq_confidence_reason = derive_eq_confidence(item, trusted_flows)
        flow_trusted = item["flowKey"] in trusted_flows
        activity_item = {
            "id": item["id"],
            "frameNumber": item["frameNumber"],
            "timeEpoch": item["timeEpoch"],
            "detectedUtc": item["detectedUtc"],
            "src": item["src"],
            "srcport": item["srcport"],
            "dst": item["dst"],
            "dstport": item["dstport"],
            "length": item["length"],
            "info": item["info"],
            "payloadPrefix": item["payloadPrefix"],
            "opcode": item["opcode"],
            "names": item["names"],
            "analysisSource": item["analysisSource"],
            "candidateCount": item["candidateCount"],
            "hasCandidates": item["hasCandidates"],
            "candidates": item["candidates"],
            "eqLike": flow_trusted or eq_like,
            "eqLikeReason": f"trusted-flow:{eq_confidence_reason}" if flow_trusted and eq_confidence_reason else (eq_like_reason or ""),
            "eqConfidence": eq_confidence,
            "eqConfidenceReason": eq_confidence_reason,
            "flowTrusted": flow_trusted,
            "ignored": item["ignored"],
            "ignoredReason": item["ignoredReason"],
        }
        activity.append(activity_item)

        if not item["hasCandidates"]:
            continue

        detections.append(
            {
                "id": item["id"],
                "frameNumber": item["frameNumber"],
                "timeEpoch": item["timeEpoch"],
                "detectedUtc": item["detectedUtc"],
                "src": item["src"],
                "srcport": item["srcport"],
                "dst": item["dst"],
                "dstport": item["dstport"],
                "length": item["length"],
                "info": item["info"],
                "payloadPrefix": item["payloadPrefix"],
                "opcode": item["opcode"],
                "names": item["names"],
                "analysisSource": item["analysisSource"],
                "candidates": item["candidates"],
                "eqLike": activity_item["eqLike"],
                "eqLikeReason": activity_item["eqLikeReason"],
                "eqConfidence": eq_confidence,
                "eqConfidenceReason": eq_confidence_reason,
                "flowTrusted": flow_trusted,
            }
        )

    return activity, detections, highest_frame


def is_session_stopped(meta_path: Path) -> bool:
    meta = load_json(meta_path)
    return bool(meta.get("StoppedUtc"))


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Monitor a live EQEmu tshark session and publish opcode detections.")
    parser.add_argument("--session-name", required=True)
    parser.add_argument("--client", required=True)
    parser.add_argument("--tshark-path")
    parser.add_argument("--repo-root")
    parser.add_argument("--captures-root")
    parser.add_argument("--poll-interval", type=float, default=2.0)
    parser.add_argument("--history-limit", type=int, default=5000)
    parser.add_argument("--activity-limit", type=int, default=5000)
    parser.add_argument("--once", action="store_true", help="Process available packets once and exit.")
    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()

    try:
        repo_root = find_repo_root(args.repo_root)
        tshark_path = find_tshark_path(args.tshark_path)
        captures_root = Path(args.captures_root).expanduser().resolve() if args.captures_root else CAPTURES_ROOT_HINT
        client_key = normalize_client(args.client)
        mapping = load_patch_map(repo_root, client_key)
        paths = session_paths(args.session_name, captures_root)
        if not wait_for_capture(paths["capture"], 10.0):
            raise FileNotFoundError(f"Capture file not found: {paths['capture']}")

        prior = load_json(paths["detections"])
        last_frame = int(prior.get("lastFrameNumber", 0))
        all_activity = prior.get("activity", [])
        all_detections = prior.get("detections", [])

        while True:
            rows = run_tshark_export(tshark_path, str(paths["capture"]), last_frame)
            new_activity, new_detections, highest_frame = build_activity_and_detections(rows, mapping)
            if highest_frame > last_frame:
                last_frame = highest_frame

            if new_activity:
                all_activity.extend(new_activity)
                if len(all_activity) > args.activity_limit:
                    all_activity = all_activity[-args.activity_limit:]

            if new_detections:
                all_detections.extend(new_detections)
                if len(all_detections) > args.history_limit:
                    all_detections = all_detections[-args.history_limit:]

            payload = {
                "sessionName": args.session_name,
                "client": display_client(client_key),
                "lastFrameNumber": last_frame,
                "activityCount": len(all_activity),
                "activity": all_activity,
                "detectionCount": len(all_detections),
                "detections": all_detections,
                "syncedUtc": datetime.now(timezone.utc).isoformat(),
            }
            write_json(paths["detections"], payload)

            if args.once:
                break

            if is_session_stopped(paths["meta"]):
                break

            time.sleep(max(args.poll_interval, 0.5))

        return 0
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
