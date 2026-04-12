import json
import mimetypes
import subprocess
import sys
import time
from http import HTTPStatus
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlparse

from occ_runtime import (
    CAPTURES_ROOT,
    CREATE_NO_WINDOW,
    HOST,
    LIVE_SESSION_PATH,
    PORT,
    REPO_ROOT,
    ROOT,
    find_tshark_path,
    list_capture_interfaces as runtime_list_capture_interfaces,
    open_path,
)

WINDOWS_SESSION_SCRIPT = ROOT / "scripts" / "invoke_occ_tshark_session.ps1"
PYTHON_SESSION_SCRIPT = ROOT / "scripts" / "occ_session.py"
LIVE_SESSION_PATH = ROOT / "data" / "live-session.json"
REGISTRY_BUILD_SCRIPT = ROOT / "scripts" / "build_rof2_reference.py"
REGISTRY_DATA_PATH = ROOT / "data" / "rof2-reference.json"


def load_live_session():
    if not LIVE_SESSION_PATH.exists():
        return {
            "status": "idle",
            "sessionName": "",
            "markers": [],
            "markerCount": 0,
            "activityCount": 0,
            "activity": [],
            "detectionCount": 0,
            "detections": [],
            "lastFrameNumber": 0,
            "syncedUtc": "",
        }

    try:
        return json.loads(LIVE_SESSION_PATH.read_text(encoding="utf-8-sig"))
    except Exception:
        return {
            "status": "idle",
            "sessionName": "",
            "markers": [],
            "markerCount": 0,
            "activityCount": 0,
            "activity": [],
            "detectionCount": 0,
            "detections": [],
            "lastFrameNumber": 0,
            "syncedUtc": "",
        }


def load_json(path):
    if not path:
        return {}

    json_path = Path(path)
    if not json_path.exists():
        return {}

    try:
        return json.loads(json_path.read_text(encoding="utf-8-sig"))
    except Exception:
        return {}


def load_markers(path):
    if not path:
        return []

    markers_path = Path(path)
    if not markers_path.exists():
        return []

    markers = []
    try:
        for index, raw_line in enumerate(markers_path.read_text(encoding="utf-8-sig").splitlines()):
            line = raw_line.strip()
            if not line:
                continue
            marker = json.loads(line)
            markers.append(
                {
                    "Id": marker.get("Id") or f"{marker.get('MarkedUtc', '')}|{marker.get('Label', 'marker')}|{index}",
                    "Index": marker.get("Index", index),
                    "Label": marker.get("Label", "marker"),
                    "Note": marker.get("Note", ""),
                    "MarkedUtc": marker.get("MarkedUtc", ""),
                    "MarkedEpoch": marker.get("MarkedEpoch", 0),
                }
            )
    except Exception:
        return []

    return markers


def rescan_opcode_registry():
    if not REGISTRY_BUILD_SCRIPT.exists():
        raise FileNotFoundError(f"Missing registry build script: {REGISTRY_BUILD_SCRIPT}")

    completed = subprocess.run(
        [sys.executable, str(REGISTRY_BUILD_SCRIPT)],
        cwd=str(ROOT),
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        creationflags=CREATE_NO_WINDOW,
        timeout=90,
        check=False,
    )
    if completed.returncode != 0:
        details = (completed.stderr or completed.stdout or "").strip()
        raise RuntimeError(details or f"Opcode registry rescan failed with exit code {completed.returncode}.")

    registry = load_json(REGISTRY_DATA_PATH)
    if not isinstance(registry, dict) or not isinstance(registry.get("entries"), list):
        raise RuntimeError(f"Opcode registry output was not written to {REGISTRY_DATA_PATH}.")

    message = (completed.stdout or "").strip() or f"Rescanned EQEmu opcodes and loaded {len(registry['entries'])} entries."
    return {
        "message": message,
        "registry": registry,
        "entryCount": len(registry["entries"]),
    }


def hydrate_session_payload(session):
    if not session:
        return {
            "status": "idle",
            "sessionName": "",
            "markers": [],
            "markerCount": 0,
            "activityCount": 0,
            "activity": [],
            "detectionCount": 0,
            "detections": [],
            "lastFrameNumber": 0,
            "syncedUtc": "",
        }

    payload = dict(session)

    live = load_live_session()
    if payload.get("sessionName") and live.get("sessionName") == payload.get("sessionName"):
        for key in (
            "status",
            "markers",
            "markerCount",
            "activity",
            "activityCount",
            "detections",
            "detectionCount",
            "lastFrameNumber",
            "syncedUtc",
        ):
            if key in live:
                payload[key] = live[key]

    markers = load_markers(payload.get("markersPath"))
    if markers:
        payload["markers"] = markers
        payload["markerCount"] = len(markers)
    else:
        payload.setdefault("markers", [])
        payload["markerCount"] = len(payload.get("markers", []))

    detections = load_json(payload.get("detectionsPath"))
    if detections:
        payload["activityCount"] = int(detections.get("activityCount") or len(detections.get("activity") or []))
        payload["activity"] = detections.get("activity") or []
        payload["detectionCount"] = int(detections.get("detectionCount") or len(detections.get("detections") or []))
        payload["detections"] = detections.get("detections") or []
        payload["lastFrameNumber"] = int(detections.get("lastFrameNumber") or 0)
        payload["syncedUtc"] = detections.get("syncedUtc") or payload.get("syncedUtc") or ""
    else:
        payload.setdefault("activity", [])
        payload.setdefault("activityCount", len(payload.get("activity", [])))
        payload.setdefault("detections", [])
        payload.setdefault("detectionCount", len(payload.get("detections", [])))
        payload.setdefault("lastFrameNumber", 0)
        payload.setdefault("syncedUtc", "")

    return payload


def resolve_session_name(requested):
    if requested:
        return requested
    live = load_live_session()
    return live.get("sessionName") or "rof2-live-ui"


def get_requested_action(payload):
    return str(payload.get("action") or "").strip().lower()


def build_session_command(payload):
    requested_action = get_requested_action(payload)
    action_map = {
        "start": "Start",
        "startcapture": "Start",
        "start capture": "Start",
        "stop": "Stop",
        "stopcapture": "Stop",
        "stop capture": "Stop",
        "mark": "Mark",
        "addmarker": "Mark",
        "add marker": "Mark",
        "status": "Status",
        "clearmarkers": "ClearMarkers",
        "clear-markers": "ClearMarkers",
        "clear_markers": "ClearMarkers",
        "cleardetections": "ClearDetections",
        "clear-detections": "ClearDetections",
        "clear_detections": "ClearDetections",
        "clearfeed": "ClearDetections",
        "clear-feed": "ClearDetections",
        "clear_feed": "ClearDetections",
    }
    action = action_map.get(requested_action)
    if action not in {"Start", "Stop", "Mark", "Status", "ClearMarkers", "ClearDetections"}:
        raise ValueError("Unsupported session action.")

    session_name = resolve_session_name(str(payload.get("sessionName") or "").strip())
    if sys.platform.startswith("win"):
        if not WINDOWS_SESSION_SCRIPT.exists():
            raise RuntimeError(f"Missing OCC session helper: {WINDOWS_SESSION_SCRIPT}")
        command = [
            "powershell",
            "-ExecutionPolicy",
            "Bypass",
            "-File",
            str(WINDOWS_SESSION_SCRIPT),
            "-Action",
            action,
            "-SessionName",
            session_name,
            "-Client",
            "RoF2",
        ]
    else:
        if not PYTHON_SESSION_SCRIPT.exists():
            raise RuntimeError(f"Missing OCC session helper: {PYTHON_SESSION_SCRIPT}")
        action_map = {
            "Start": "start",
            "Stop": "stop",
            "Mark": "mark",
            "Status": "status",
            "ClearMarkers": "clear-markers",
            "ClearDetections": "clear-detections",
        }
        command = [
            sys.executable,
            str(PYTHON_SESSION_SCRIPT),
            "--action",
            action_map[action],
            "--session-name",
            session_name,
            "--client",
            "RoF2",
        ]

    if action == "Start":
        if sys.platform.startswith("win"):
            command.extend([
                "-Interface",
                str(payload.get("interface") or "Ethernet"),
                "-CaptureFilter",
                str(payload.get("captureFilter") or "udp"),
                "-DurationSec",
                str(int(payload.get("durationSec") or 0)),
            ])
        else:
            command.extend([
                "--interface",
                str(payload.get("interface") or "Ethernet"),
                "--capture-filter",
                str(payload.get("captureFilter") or "udp"),
                "--duration-sec",
                str(int(payload.get("durationSec") or 0)),
            ])
    elif action == "Mark":
        if sys.platform.startswith("win"):
            command.extend([
                "-Label",
                str(payload.get("label") or "marker"),
                "-Note",
                str(payload.get("note") or ""),
            ])
        else:
            command.extend([
                "--label",
                str(payload.get("label") or "marker"),
                "--note",
                str(payload.get("note") or ""),
            ])

    return action, session_name, command


def wait_for_session(session_name, predicate, timeout_seconds=8.0):
    deadline = time.time() + timeout_seconds
    latest = load_live_session()
    while time.time() < deadline:
        latest = load_live_session()
        if latest.get("sessionName") == session_name and predicate(latest):
            return latest
        time.sleep(0.2)
    return latest


def run_session_script(payload):
    requested_action = get_requested_action(payload)
    if requested_action == "toggle":
        live = load_live_session()
        payload = {
            **payload,
            "action": "stop" if live.get("status") == "running" else "start",
        }
    elif requested_action == "restart":
        live = load_live_session()
        session_name = resolve_session_name(str(payload.get("sessionName") or "").strip())
        if live.get("status") == "running" and live.get("sessionName") == session_name:
            run_session_script({**payload, "action": "stop", "sessionName": session_name})
        payload = {
            **payload,
            "action": "start",
            "sessionName": session_name,
        }

    action, session_name, command = build_session_command(payload)

    if action == "Start":
        process = subprocess.Popen(
            command,
            cwd=str(REPO_ROOT),
            stdin=subprocess.DEVNULL,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.PIPE,
            text=True,
            creationflags=CREATE_NO_WINDOW,
        )
        session = wait_for_session(session_name, lambda live: live.get("status") == "running")

        if session.get("sessionName") == session_name and session.get("status") == "running":
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=2)
                except subprocess.TimeoutExpired:
                    process.kill()
            if process.stderr:
                process.stderr.close()
            return {
                "message": f"Started session '{session_name}'",
                "stdout": "",
                "stderr": "",
                "session": session,
            }

        if process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=2)
            except subprocess.TimeoutExpired:
                process.kill()
        if process.stderr:
            process.stderr.close()
        raise RuntimeError(f"Timed out waiting for session '{session_name}' to start.")

    result = subprocess.run(
        command,
        cwd=str(REPO_ROOT),
        capture_output=True,
        text=True,
        timeout=20,
        check=False,
        creationflags=CREATE_NO_WINDOW,
    )
    stdout = result.stdout.strip()
    stderr = result.stderr.strip()
    if result.returncode != 0:
        raise RuntimeError(stderr or stdout or f"Session action failed with exit code {result.returncode}.")

    return {
        "message": stdout.splitlines()[0] if stdout else f"{action} completed.",
        "stdout": stdout,
        "stderr": stderr,
        "session": load_live_session(),
    }


def get_fresh_session_state():
    live = load_live_session()
    session_name = live.get("sessionName")
    if not session_name:
        return hydrate_session_payload(live)

    try:
        session = run_session_script({"action": "status", "sessionName": session_name}).get("session") or live
        return hydrate_session_payload(session)
    except Exception:
        return hydrate_session_payload(live)


def list_capture_interfaces():
    tshark_path = find_tshark_path()
    if not tshark_path:
        launcher = "utils\\occ\\start_occ.ps1" if sys.platform.startswith("win") else "utils/occ/start_occ.sh"
        raise RuntimeError(f"tshark is not installed. Run {launcher} for guided setup.")

    return runtime_list_capture_interfaces(tshark_path)


class OccRequestHandler(SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=str(ROOT), **kwargs)

    def end_headers(self):
        self.send_header("Cache-Control", "no-store, max-age=0")
        super().end_headers()

    def log_message(self, format, *args):
        return

    def do_GET(self):
        parsed = urlparse(self.path)
        if parsed.path == "/api/session":
            self.respond_json({"ok": True, "session": get_fresh_session_state()})
            return
        if parsed.path == "/api/interfaces":
            try:
                self.respond_json({"ok": True, "interfaces": list_capture_interfaces()})
            except Exception as exc:
                self.respond_json({"ok": False, "error": str(exc)}, status=HTTPStatus.INTERNAL_SERVER_ERROR)
            return
        return super().do_GET()

    def do_POST(self):
        parsed = urlparse(self.path)
        if parsed.path == "/api/open-captures-folder":
            try:
                CAPTURES_ROOT.mkdir(parents=True, exist_ok=True)
                open_path(CAPTURES_ROOT)
                self.respond_json({"ok": True, "path": str(CAPTURES_ROOT)})
            except Exception as exc:
                self.respond_json({"ok": False, "error": str(exc)}, status=HTTPStatus.INTERNAL_SERVER_ERROR)
            return

        if parsed.path == "/api/rescan-opcodes":
            try:
                self.respond_json({"ok": True, **rescan_opcode_registry()})
            except subprocess.TimeoutExpired:
                self.respond_json(
                    {"ok": False, "error": "Opcode registry rescan timed out after 90 seconds."},
                    status=HTTPStatus.GATEWAY_TIMEOUT,
                )
            except Exception as exc:
                self.respond_json({"ok": False, "error": str(exc)}, status=HTTPStatus.INTERNAL_SERVER_ERROR)
            return

        if parsed.path != "/api/session":
            self.send_error(HTTPStatus.NOT_FOUND)
            return

        try:
            length = int(self.headers.get("Content-Length", "0"))
            body = self.rfile.read(length) if length else b"{}"
            payload = json.loads(body.decode("utf-8"))
            result = run_session_script(payload)
            self.respond_json({"ok": True, **result})
        except ValueError as exc:
            self.respond_json({"ok": False, "error": str(exc)}, status=HTTPStatus.BAD_REQUEST)
        except RuntimeError as exc:
            self.respond_json({"ok": False, "error": str(exc), "session": load_live_session()}, status=HTTPStatus.BAD_REQUEST)
        except Exception as exc:
            self.respond_json({"ok": False, "error": str(exc)}, status=HTTPStatus.INTERNAL_SERVER_ERROR)

    def guess_type(self, path):
        if path.endswith(".js"):
            return "application/javascript"
        return mimetypes.guess_type(path)[0] or "application/octet-stream"

    def respond_json(self, payload, status=HTTPStatus.OK):
        body = json.dumps(payload).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


def main():
    server = ThreadingHTTPServer((HOST, PORT), OccRequestHandler)
    print(f"OCC available at http://{HOST}:{PORT}/", flush=True)
    server.serve_forever()


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        sys.exit(0)
