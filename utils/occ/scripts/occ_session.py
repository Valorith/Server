#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

from occ_runtime import (
    CAPTURES_ROOT,
    LIVE_SESSION_PATH,
    REPO_ROOT,
    ROOT,
    find_tshark_path,
    process_exists,
    popen_command,
    read_json,
    resolve_tshark_interface,
    terminate_process,
    wait_for_process_exit,
    write_json,
)


MONITOR_SCRIPT = ROOT / "scripts" / "monitor_occ_live.py"


def session_paths(session_name: str) -> dict[str, Path]:
    root = CAPTURES_ROOT / session_name
    return {
        "root": root,
        "meta": root / "session.json",
        "markers": root / "markers.jsonl",
        "capture": root / f"{session_name}.pcapng",
        "stdout": root / "tshark.stdout.log",
        "stderr": root / "tshark.stderr.log",
        "detections": root / "detections.json",
        "monitor_stdout": root / "monitor.stdout.log",
        "monitor_stderr": root / "monitor.stderr.log",
    }


def read_session_markers(path: Path) -> list[dict]:
    if not path.is_file():
        return []

    markers = []
    for raw_line in path.read_text(encoding="utf-8-sig", errors="ignore").splitlines():
        line = raw_line.strip()
        if not line:
            continue
        try:
            markers.append(json.loads(line))
        except json.JSONDecodeError:
            continue
    return markers


def read_session_detections(path: Path) -> dict:
    return read_json(
        path,
        {
            "activityCount": 0,
            "activity": [],
            "detectionCount": 0,
            "detections": [],
            "lastFrameNumber": 0,
            "syncedUtc": "",
        },
    )


def write_session_meta(path: Path, payload: dict) -> None:
    write_json(path, payload)


def export_live_session_state(session_name: str, paths: dict[str, Path], meta: dict | None) -> None:
    if not LIVE_SESSION_PATH.parent.exists():
        return

    running = bool(meta and process_exists(meta.get("ProcessId")))
    markers = read_session_markers(paths["markers"])
    detections = read_session_detections(paths["detections"])

    if not meta:
        status = "idle"
    elif running:
        status = "running"
    elif meta.get("StoppedUtc") or paths["capture"].exists():
        status = "stopped"
    else:
        status = "idle"

    marker_payload = []
    for index, marker in enumerate(markers):
        marker_payload.append(
            {
                "Id": marker.get("Id") or f"{marker.get('MarkedUtc', '')}|{marker.get('Label', 'marker')}|{index}",
                "Index": marker.get("Index", index),
                "Label": marker.get("Label", "marker"),
                "Note": marker.get("Note", ""),
                "MarkedUtc": marker.get("MarkedUtc", ""),
                "MarkedEpoch": marker.get("MarkedEpoch", 0),
            }
        )

    payload = {
        "status": status,
        "sessionName": meta.get("SessionName", session_name) if meta else session_name,
        "interface": meta.get("Interface", "") if meta else "",
        "resolvedInterface": meta.get("ResolvedInterface", "") if meta else "",
        "captureFilter": meta.get("CaptureFilter", "") if meta else "",
        "client": meta.get("Client", "") if meta else "",
        "capturePath": meta.get("CapturePath", "") if meta else "",
        "markersPath": meta.get("MarkersPath", "") if meta else "",
        "detectionsPath": meta.get("DetectionsPath", "") if meta else "",
        "startedUtc": meta.get("StartedUtc", "") if meta else "",
        "stoppedUtc": meta.get("StoppedUtc", "") if meta else "",
        "markerCount": len(marker_payload),
        "markers": marker_payload,
        "activityCount": int(detections.get("activityCount") or 0),
        "activity": detections.get("activity") or [],
        "detectionCount": int(detections.get("detectionCount") or 0),
        "detections": detections.get("detections") or [],
        "lastFrameNumber": int(detections.get("lastFrameNumber") or 0),
        "syncedUtc": detections.get("syncedUtc") or datetime.now(timezone.utc).isoformat(),
    }
    write_json(LIVE_SESSION_PATH, payload)


def remove_existing_session_files(paths: dict[str, Path]) -> None:
    for key in ("capture", "markers", "stdout", "stderr", "detections", "monitor_stdout", "monitor_stderr"):
        try:
            paths[key].unlink(missing_ok=True)
        except Exception:
            continue


def start_capture(args, paths: dict[str, Path], meta: dict | None) -> int:
    if meta and process_exists(meta.get("ProcessId")):
        raise RuntimeError(f"Session '{args.session_name}' is already running with PID {meta['ProcessId']}.")

    tshark_path = find_tshark_path(args.tshark_path)
    if not tshark_path:
        raise FileNotFoundError("tshark was not found. Install it before starting an OCC capture session.")

    resolved_interface = resolve_tshark_interface(tshark_path, args.interface)
    paths["root"].mkdir(parents=True, exist_ok=True)
    remove_existing_session_files(paths)

    tshark_command = [
        str(tshark_path),
        "-i",
        resolved_interface,
        "-w",
        str(paths["capture"]),
        "-q",
    ]
    if args.capture_filter.strip():
        tshark_command.extend(["-f", args.capture_filter.strip()])
    if args.duration_sec > 0:
        tshark_command.extend(["-a", f"duration:{args.duration_sec}"])

    with paths["stdout"].open("w", encoding="utf-8") as stdout_handle, paths["stderr"].open("w", encoding="utf-8") as stderr_handle:
        process = popen_command(
            tshark_command,
            stdout=stdout_handle,
            stderr=stderr_handle,
            stdin=subprocess.DEVNULL,
            cwd=str(REPO_ROOT),
            start_new_session=not sys.platform.startswith("win"),
        )

    time.sleep(0.3)
    now = datetime.now(timezone.utc)
    meta = {
        "SessionName": args.session_name,
        "Client": args.client,
        "ProcessId": process.pid,
        "CapturePath": str(paths["capture"]),
        "MarkersPath": str(paths["markers"]),
        "DetectionsPath": str(paths["detections"]),
        "StdOutPath": str(paths["stdout"]),
        "StdErrPath": str(paths["stderr"]),
        "Interface": args.interface,
        "ResolvedInterface": resolved_interface,
        "CaptureFilter": args.capture_filter,
        "StartedUtc": now.isoformat(),
        "StartedEpoch": now.timestamp(),
        "TsharkPath": str(tshark_path),
        "RepoRoot": str(REPO_ROOT),
    }
    write_session_meta(paths["meta"], meta)

    monitor_command = [
        sys.executable,
        "-u",
        str(MONITOR_SCRIPT),
        "--session-name",
        args.session_name,
        "--client",
        args.client,
        "--repo-root",
        str(REPO_ROOT),
        "--captures-root",
        str(CAPTURES_ROOT),
        "--tshark-path",
        str(tshark_path),
    ]
    with paths["monitor_stdout"].open("w", encoding="utf-8") as monitor_stdout, paths["monitor_stderr"].open("w", encoding="utf-8") as monitor_stderr:
        monitor_process = popen_command(
            monitor_command,
            stdout=monitor_stdout,
            stderr=monitor_stderr,
            stdin=subprocess.DEVNULL,
            cwd=str(REPO_ROOT),
            start_new_session=not sys.platform.startswith("win"),
        )

    meta["MonitorProcessId"] = monitor_process.pid
    write_session_meta(paths["meta"], meta)
    export_live_session_state(args.session_name, paths, meta)
    print(f"Started session '{args.session_name}' with PID {process.pid}")
    print(f"Capture file: {paths['capture']}")
    return 0


def add_marker(args, paths: dict[str, Path], meta: dict | None) -> int:
    if not meta or not process_exists(meta.get("ProcessId")):
        raise RuntimeError(f"Session '{args.session_name}' is not running.")

    now = datetime.now(timezone.utc)
    marker = {
        "Label": args.label,
        "Note": args.note,
        "MarkedUtc": now.isoformat(),
        "MarkedEpoch": now.timestamp(),
    }
    paths["markers"].parent.mkdir(parents=True, exist_ok=True)
    with paths["markers"].open("a", encoding="utf-8") as handle:
        handle.write(json.dumps(marker, ensure_ascii=True, separators=(",", ":")) + "\n")

    export_live_session_state(args.session_name, paths, meta)
    print(f"Marked '{args.label}' at {marker['MarkedUtc']}")
    return 0


def stop_capture(args, paths: dict[str, Path], meta: dict | None) -> int:
    if not meta:
        raise RuntimeError(f"Session '{args.session_name}' does not exist.")

    process_id = meta.get("ProcessId")
    if process_exists(process_id):
        terminate_process(int(process_id), force=True)
        wait_for_process_exit(int(process_id), timeout_seconds=5.0)

    meta["StoppedUtc"] = datetime.now(timezone.utc).isoformat()
    write_session_meta(paths["meta"], meta)
    time.sleep(1.2)

    monitor_pid = meta.get("MonitorProcessId")
    if process_exists(monitor_pid):
        try:
            terminate_process(int(monitor_pid), force=False)
            wait_for_process_exit(int(monitor_pid), timeout_seconds=2.0)
        except Exception:
            try:
                terminate_process(int(monitor_pid), force=True)
            except Exception:
                pass

    export_live_session_state(args.session_name, paths, meta)
    print(f"Stopped session '{args.session_name}'")
    print(f"Capture file: {meta.get('CapturePath', str(paths['capture']))}")
    return 0


def show_status(args, paths: dict[str, Path], meta: dict | None) -> int:
    if not meta:
        raise RuntimeError(f"Session '{args.session_name}' does not exist.")

    running = process_exists(meta.get("ProcessId"))
    print(f"Session: {meta.get('SessionName', args.session_name)}")
    print(f"Running: {running}")
    print(f"PID: {meta.get('ProcessId', '')}")
    print(f"Capture: {meta.get('CapturePath', str(paths['capture']))}")
    print(f"Markers: {meta.get('MarkersPath', str(paths['markers']))}")
    print(f"Detections: {meta.get('DetectionsPath', str(paths['detections']))}")
    print(f"Filter: {meta.get('CaptureFilter', '')}")
    export_live_session_state(args.session_name, paths, meta)
    return 0


def clear_markers(args, paths: dict[str, Path], meta: dict | None) -> int:
    if not meta:
        raise RuntimeError(f"Session '{args.session_name}' does not exist.")

    paths["markers"].parent.mkdir(parents=True, exist_ok=True)
    paths["markers"].write_text("", encoding="utf-8")
    export_live_session_state(args.session_name, paths, meta)
    print(f"Cleared markers for session '{args.session_name}'")
    return 0


def clear_detections(args, paths: dict[str, Path], meta: dict | None) -> int:
    if not meta:
        raise RuntimeError(f"Session '{args.session_name}' does not exist.")

    current = read_session_detections(paths["detections"])
    cleared = {
        "activityCount": 0,
        "activity": [],
        "detectionCount": 0,
        "detections": [],
        "lastFrameNumber": int(current.get("lastFrameNumber") or 0),
        "syncedUtc": datetime.now(timezone.utc).isoformat(),
    }
    write_json(paths["detections"], cleared)
    export_live_session_state(args.session_name, paths, meta)
    print(f"Cleared detections for session '{args.session_name}'")
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Cross-platform OCC tshark session helper.")
    parser.add_argument("--action", required=True, choices=["start", "mark", "stop", "status", "clear-markers", "clear-detections"])
    parser.add_argument("--session-name", default="occ-live")
    parser.add_argument("--client", default="RoF2")
    parser.add_argument("--tshark-path", default="")
    parser.add_argument("--interface", default="Ethernet")
    parser.add_argument("--capture-filter", default="udp")
    parser.add_argument("--duration-sec", type=int, default=0)
    parser.add_argument("--label", default="marker")
    parser.add_argument("--note", default="")
    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()

    paths = session_paths(args.session_name)
    paths["root"].mkdir(parents=True, exist_ok=True)
    meta = read_json(paths["meta"], None)

    try:
        if args.action == "start":
            return start_capture(args, paths, meta)
        if args.action == "mark":
            return add_marker(args, paths, meta)
        if args.action == "stop":
            return stop_capture(args, paths, meta)
        if args.action == "status":
            return show_status(args, paths, meta)
        if args.action == "clear-markers":
            return clear_markers(args, paths, meta)
        if args.action == "clear-detections":
            return clear_detections(args, paths, meta)
        raise RuntimeError(f"Unsupported action '{args.action}'.")
    except Exception as exc:
        print(str(exc), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
