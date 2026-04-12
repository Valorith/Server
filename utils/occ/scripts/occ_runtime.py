from __future__ import annotations

import json
import os
import re
import shutil
import signal
import socket
import subprocess
import sys
import time
import webbrowser
from pathlib import Path
from urllib.error import HTTPError, URLError
from urllib.request import urlopen

if sys.platform.startswith("win"):
    import ctypes
    from ctypes import wintypes


ROOT = Path(__file__).resolve().parent.parent
REPO_ROOT = ROOT.parent.parent
WORKSPACE_ROOT = REPO_ROOT.parent
SERVER_SCRIPT = ROOT / "scripts" / "occ_server.py"
SERVER_STDOUT_LOG = ROOT / "occ_server.start.log"
SERVER_STDERR_LOG = ROOT / "occ_server.error.log"
SERVER_PID_PATH = ROOT / "data" / "occ_server.pid"
LIVE_SESSION_PATH = ROOT / "data" / "live-session.json"
CAPTURES_ROOT = WORKSPACE_ROOT / ".codex" / "captures" / "sessions"
HOST = "127.0.0.1"
PORT = 8765
URL = f"http://{HOST}:{PORT}/"
CREATE_NO_WINDOW = getattr(subprocess, "CREATE_NO_WINDOW", 0)
IS_WINDOWS = sys.platform.startswith("win")
IS_MACOS = sys.platform == "darwin"
INTERFACE_LINE_RE = re.compile(r"^\s*(?P<index>\d+)\.\s+(?P<body>.+?)\s*$")
WINDOWS_TSHARK_PATHS = (
    Path(r"C:\Program Files\Wireshark\tshark.exe"),
    Path(r"C:\Program Files (x86)\Wireshark\tshark.exe"),
)
MACOS_TSHARK_PATHS = (
    Path("/Applications/Wireshark.app/Contents/MacOS/tshark"),
    Path("/opt/homebrew/bin/tshark"),
    Path("/usr/local/bin/tshark"),
)
LINUX_TSHARK_PATHS = (
    Path("/usr/bin/tshark"),
    Path("/usr/local/bin/tshark"),
)
EXTCAP_INTERFACE_NAMES = {
    "androiddump",
    "ciscodump",
    "dpauxmon",
    "etw",
    "randpkt",
    "randpktdump",
    "sdjournal",
    "sshdump",
    "udpdump",
    "wifidump",
}

if IS_WINDOWS:
    PROCESS_QUERY_LIMITED_INFORMATION = 0x1000
    SYNCHRONIZE = 0x00100000
    STILL_ACTIVE = 259
    _KERNEL32 = ctypes.WinDLL("kernel32", use_last_error=True)
    _OPEN_PROCESS = _KERNEL32.OpenProcess
    _OPEN_PROCESS.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    _OPEN_PROCESS.restype = wintypes.HANDLE
    _GET_EXIT_CODE_PROCESS = _KERNEL32.GetExitCodeProcess
    _GET_EXIT_CODE_PROCESS.argtypes = [wintypes.HANDLE, ctypes.POINTER(wintypes.DWORD)]
    _GET_EXIT_CODE_PROCESS.restype = wintypes.BOOL
    _CLOSE_HANDLE = _KERNEL32.CloseHandle
    _CLOSE_HANDLE.argtypes = [wintypes.HANDLE]
    _CLOSE_HANDLE.restype = wintypes.BOOL


def create_subprocess_kwargs() -> dict:
    if IS_WINDOWS:
        return {"creationflags": CREATE_NO_WINDOW}
    return {}


def run_command(command: list[str], **kwargs) -> subprocess.CompletedProcess:
    return subprocess.run(command, **create_subprocess_kwargs(), **kwargs)


def popen_command(command: list[str], **kwargs) -> subprocess.Popen:
    return subprocess.Popen(command, **create_subprocess_kwargs(), **kwargs)


def read_json(path: Path, default):
    if not path.is_file():
        return default
    try:
        return json.loads(path.read_text(encoding="utf-8-sig"))
    except Exception:
        return default


def write_json(path: Path, payload: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2), encoding="utf-8")


def get_tshark_candidates() -> tuple[Path, ...]:
    if IS_WINDOWS:
        return WINDOWS_TSHARK_PATHS
    if IS_MACOS:
        return MACOS_TSHARK_PATHS
    return LINUX_TSHARK_PATHS


def find_tshark_path(explicit: str | None = None) -> Path | None:
    if explicit:
        candidate = Path(explicit).expanduser().resolve()
        if candidate.is_file():
            return candidate

    from_path = shutil.which("tshark")
    if from_path:
        candidate = Path(from_path).resolve()
        if candidate.is_file():
            return candidate

    for candidate in get_tshark_candidates():
        if candidate.is_file():
            return candidate

    return None


def get_tool_version_line(executable: str | Path, arguments: list[str]) -> str:
    try:
        completed = run_command(
            [str(executable), *arguments],
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=10,
            check=False,
        )
    except Exception:
        return ""

    output = (completed.stdout or completed.stderr or "").splitlines()
    return output[0].strip() if output else ""


def _parse_interface_line(raw_line: str) -> dict | None:
    match = INTERFACE_LINE_RE.match(raw_line.strip())
    if not match:
        return None

    body = match.group("body").strip()
    device_match = re.match(r"^(?P<device>\S+)(?:\s+\((?P<description>.*)\))?$", body)
    if not device_match:
        return None

    device = (device_match.group("device") or "").strip()
    description = (device_match.group("description") or "").strip()

    device = device.strip()
    if not device:
        return None

    return {
        "index": match.group("index"),
        "device": device,
        "description": description,
    }


def _is_loopback_interface(device: str, description: str) -> bool:
    tokens = f"{device} {description}".strip().lower()
    if "loopback" in tokens:
        return True
    return device.lower() in {"lo", "lo0", r"\device\npf_loopback"}


def _is_extcap_interface(device: str, description: str) -> bool:
    device_key = device.strip().lower()
    description_key = description.strip().lower()
    if device_key in EXTCAP_INTERFACE_NAMES:
        return True
    if "remote capture" in description_key or "extcap" in description_key:
        return True
    if "random packet" in description_key or "journal export" in description_key:
        return True
    if "://" in device_key:
        return True
    return False


def list_capture_interfaces(tshark_path: Path) -> list[dict]:
    completed = run_command(
        [str(tshark_path), "-D"],
        cwd=str(REPO_ROOT),
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        timeout=10,
        check=False,
    )
    if completed.returncode != 0:
        raise RuntimeError(completed.stderr.strip() or completed.stdout.strip() or "Failed to list tshark interfaces.")

    entries: list[dict] = []
    seen_values: set[str] = set()

    for raw_line in completed.stdout.splitlines():
        parsed = _parse_interface_line(raw_line)
        if not parsed:
            continue

        device = parsed["device"]
        description = parsed["description"]
        is_loopback = _is_loopback_interface(device, description)
        is_extcap = _is_extcap_interface(device, description)

        if IS_WINDOWS and not (device.startswith(r"\Device\NPF_") or device == r"\Device\NPF_Loopback"):
            continue

        if is_loopback:
            value = "loopback"
            label = "Loopback"
            description_text = description or "Adapter for loopback traffic capture"
        elif IS_WINDOWS:
            value = description or device
            label = description or device
            description_text = description
        else:
            value = device
            label = description or device
            description_text = description

        if value in seen_values:
            continue
        seen_values.add(value)

        preferred_name = f"{value} {label}".lower()
        if any(token in preferred_name for token in ("ethernet", "wi-fi", "wifi", "wlan")):
            priority = 0
        elif not is_loopback and not is_extcap:
            priority = 1
        elif is_loopback:
            priority = 2
        else:
            priority = 3

        entries.append(
            {
                "index": parsed["index"],
                "value": value,
                "label": label,
                "device": device,
                "description": description_text,
                "isLoopback": is_loopback,
                "isExtcap": is_extcap,
                "_priority": priority,
            }
        )

    entries.sort(key=lambda item: (item["_priority"], item["label"].lower(), item["device"].lower()))
    for entry in entries:
        entry.pop("_priority", None)
    return entries


def resolve_tshark_interface(tshark_path: Path, requested_interface: str) -> str:
    requested = (requested_interface or "").strip()
    if not requested:
        requested = "loopback"

    if requested.isdigit():
        return requested

    interfaces = list_capture_interfaces(tshark_path)
    if not interfaces:
        raise RuntimeError("No tshark capture interfaces are available.")

    lowered = requested.lower()
    if lowered == "loopback":
        for entry in interfaces:
            if entry.get("isLoopback"):
                return str(entry["device"])
        raise RuntimeError("No loopback capture interface is available on this system.")

    exact_matches = []
    partial_matches = []
    for entry in interfaces:
        candidates = {
            str(entry.get("value") or "").strip().lower(),
            str(entry.get("label") or "").strip().lower(),
            str(entry.get("device") or "").strip().lower(),
            str(entry.get("description") or "").strip().lower(),
            str(entry.get("index") or "").strip().lower(),
        }
        if lowered in candidates:
            exact_matches.append(entry)
            continue

        haystack = " ".join(candidate for candidate in candidates if candidate)
        if lowered and lowered in haystack:
            partial_matches.append(entry)

    if exact_matches:
        return str(exact_matches[0]["device"])
    if len(partial_matches) == 1:
        return str(partial_matches[0]["device"])
    if len(partial_matches) > 1:
        labels = ", ".join(str(entry.get("label") or entry.get("device") or "") for entry in partial_matches)
        raise RuntimeError(f"Interface '{requested_interface}' is ambiguous. Matching labels: {labels}")

    raise RuntimeError(f"Could not resolve interface '{requested_interface}'.")


def is_port_open(host: str = HOST, port: int = PORT) -> bool:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.settimeout(0.5)
        return sock.connect_ex((host, port)) == 0


def fetch_occ_json(path: str = "/api/session") -> dict | None:
    try:
        with urlopen(f"{URL.rstrip('/')}{path}", timeout=2) as response:
            if response.status != 200:
                return None
            return json.loads(response.read().decode("utf-8"))
    except (URLError, HTTPError, TimeoutError, ValueError, json.JSONDecodeError):
        return None


def is_occ_ready() -> bool:
    payload = fetch_occ_json("/api/session")
    return isinstance(payload, dict) and payload.get("ok") is True


def process_exists(pid: int | None) -> bool:
    if not pid:
        return False
    if IS_WINDOWS:
        handle = _OPEN_PROCESS(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, False, int(pid))
        if not handle:
            error = ctypes.get_last_error()
            if error == 5:
                return True
            return False

        try:
            exit_code = wintypes.DWORD()
            if not _GET_EXIT_CODE_PROCESS(handle, ctypes.byref(exit_code)):
                error = ctypes.get_last_error()
                if error == 5:
                    return True
                return False
            return exit_code.value == STILL_ACTIVE
        finally:
            _CLOSE_HANDLE(handle)

    try:
        os.kill(int(pid), 0)
        return True
    except ProcessLookupError:
        return False
    except PermissionError:
        return True
    except OSError:
        return False


def terminate_process(pid: int, force: bool = False) -> None:
    if IS_WINDOWS:
        command = ["taskkill", "/PID", str(pid), "/T"]
        if force:
            command.append("/F")
        completed = run_command(command, capture_output=True, text=True, timeout=10, check=False)
        if completed.returncode not in (0, 128):
            detail = (completed.stderr or completed.stdout or "").strip()
            raise RuntimeError(detail or f"Failed to stop process {pid}.")
        return

    sig = signal.SIGKILL if force else signal.SIGTERM
    os.kill(int(pid), sig)


def wait_for_process_exit(pid: int, timeout_seconds: float = 5.0) -> bool:
    deadline = time.time() + timeout_seconds
    while time.time() < deadline:
        if not process_exists(pid):
            return True
        time.sleep(0.2)
    return not process_exists(pid)


def get_process_command_line(pid: int | None) -> str:
    if not pid:
        return ""

    if IS_WINDOWS:
        completed = run_command(
            [
                "powershell",
                "-NoProfile",
                "-Command",
                f"$proc = Get-CimInstance Win32_Process -Filter \"ProcessId = {int(pid)}\" -ErrorAction SilentlyContinue; if ($proc) {{ $proc.CommandLine }}",
            ],
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=10,
            check=False,
        )
        if completed.returncode != 0:
            return ""
        return (completed.stdout or "").strip()

    completed = run_command(
        ["ps", "-p", str(int(pid)), "-o", "command="],
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        timeout=10,
        check=False,
    )
    if completed.returncode != 0:
        return ""
    return (completed.stdout or "").strip()


def open_path(path: Path) -> None:
    target = str(path)
    if IS_WINDOWS:
        popen_command(
            ["explorer.exe", target],
            cwd=str(REPO_ROOT),
            stdin=subprocess.DEVNULL,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        return

    command = ["open", target] if IS_MACOS else ["xdg-open", target]
    completed = run_command(command, cwd=str(REPO_ROOT), capture_output=True, text=True, timeout=10, check=False)
    if completed.returncode != 0:
        detail = (completed.stderr or completed.stdout or "").strip()
        raise RuntimeError(detail or f"Failed to open {target}.")


def open_browser(url: str = URL) -> None:
    if not webbrowser.open(url, new=2):
        raise RuntimeError(f"Failed to open the browser for {url}.")


def read_log_tail(path: Path, line_count: int = 20) -> str:
    if not path.is_file():
        return ""
    lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    return "\n".join(lines[-line_count:]).strip()


def detect_tshark_install_plan() -> dict | None:
    if IS_WINDOWS:
        winget = shutil.which("winget")
        if not winget:
            return None
        return {
            "label": "winget",
            "command": [winget, "install", "--id", "WiresharkFoundation.Wireshark", "--exact", "--accept-package-agreements", "--accept-source-agreements"],
            "notes": [
                "Keep the TShark feature enabled during installation.",
                "If the installer offers Npcap, accept it so packet capture works.",
            ],
        }

    if IS_MACOS:
        brew = shutil.which("brew")
        if not brew:
            return None
        return {
            "label": "Homebrew",
            "command": [brew, "install", "wireshark"],
            "notes": [
                "Homebrew installs tshark and the rest of Wireshark together.",
                "macOS may prompt for additional packet-capture permissions the first time you run tshark.",
            ],
        }

    package_managers = (
        ("apt", ["sudo", "apt", "update"], ["sudo", "apt", "install", "-y", "tshark"], [
            "If non-root capture is requested, allow it during the package prompt.",
            "After installation, add your user to the wireshark group if prompted.",
        ]),
        ("dnf", None, ["sudo", "dnf", "install", "-y", "wireshark-cli"], [
            "If non-root capture is needed, add your user to the wireshark group after install.",
        ]),
        ("pacman", None, ["sudo", "pacman", "-S", "--noconfirm", "wireshark-cli"], [
            "If non-root capture is needed, add your user to the wireshark group after install.",
        ]),
        ("zypper", None, ["sudo", "zypper", "--non-interactive", "install", "wireshark"], [
            "If non-root capture is needed, add your user to the wireshark group after install.",
        ]),
    )
    for name, pre_command, install_command, notes in package_managers:
        if shutil.which(name):
            return {
                "label": name,
                "pre_command": pre_command,
                "command": install_command,
                "notes": notes,
            }
    return None


def install_tshark() -> tuple[bool, str]:
    plan = detect_tshark_install_plan()
    if not plan:
        return False, "No supported package manager was detected for automatic tshark installation."

    try:
        if plan.get("pre_command"):
            pre = run_command(plan["pre_command"], cwd=str(REPO_ROOT), check=False)
            if pre.returncode != 0:
                return False, f"{plan['label']} preflight command failed with exit code {pre.returncode}."

        completed = run_command(plan["command"], cwd=str(REPO_ROOT), check=False)
        if completed.returncode != 0:
            return False, f"{plan['label']} install command failed with exit code {completed.returncode}."
        return True, plan["label"]
    except Exception as exc:
        return False, str(exc)


def get_tshark_install_help(script_hint: str) -> str:
    lines = [
        f"OCC capture controls need tshark. Install it, then run {script_hint} again.",
    ]

    plan = detect_tshark_install_plan()
    if plan:
        command = " ".join(plan["command"])
        lines.append(f"Suggested install command: {command}")
        for note in plan.get("notes", []):
            lines.append(note)
    else:
        if IS_MACOS:
            lines.append("If you use Homebrew, install it with: brew install wireshark")
        elif IS_WINDOWS:
            lines.append("Install Wireshark with the TShark feature enabled and allow Npcap if prompted.")
        else:
            lines.append("Install tshark from your distribution packages or the Wireshark downloads page.")
            lines.append("You may also need permission to capture without running the tool as root.")

    return "\n".join(lines)
