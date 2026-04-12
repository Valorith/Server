#!/usr/bin/env python3

from __future__ import annotations

import argparse
import subprocess
import sys
import time
from pathlib import Path

from occ_runtime import (
    CAPTURES_ROOT,
    PORT,
    REPO_ROOT,
    ROOT,
    SERVER_PID_PATH,
    SERVER_SCRIPT,
    SERVER_STDERR_LOG,
    SERVER_STDOUT_LOG,
    URL,
    find_tshark_path,
    get_tool_version_line,
    get_tshark_install_help,
    install_tshark,
    is_occ_ready,
    is_port_open,
    open_browser,
    popen_command,
    process_exists,
    get_process_command_line,
    read_log_tail,
    terminate_process,
    wait_for_process_exit,
)


def write_step(message: str, tone: str = "info") -> None:
    prefix = {
        "success": "[ok]",
        "warning": "[!]",
        "error": "[x]",
    }.get(tone, "[...]")
    print(f"{prefix} {message}")


def wait_for_occ_ready(timeout_seconds: float = 15.0, allow_log_hint: bool = True) -> bool:
    deadline = time.time() + timeout_seconds
    while time.time() < deadline:
        if is_occ_ready():
            return True
        if allow_log_hint and SERVER_STDOUT_LOG.is_file():
            recent = read_log_tail(SERVER_STDOUT_LOG, line_count=5)
            if f"OCC available at {URL}" in recent:
                return True
        time.sleep(0.35)
    return is_occ_ready()


def ensure_tshark(skip_check: bool, auto_install: bool) -> Path | None:
    if skip_check:
        write_step("Skipping tshark preflight. OCC will open, but capture controls will remain unavailable until tshark is installed.", "warning")
        return None

    tshark_path = find_tshark_path()
    if tshark_path:
        version = get_tool_version_line(tshark_path, ["--version"])
        suffix = f" ({version})" if version else ""
        write_step(f"Found tshark at {tshark_path}{suffix}", "success")
        return tshark_path

    write_step(get_tshark_install_help(script_hint="this launcher"), "warning")
    if not auto_install:
        raise RuntimeError("tshark is not installed.")

    write_step("Attempting to install tshark with the detected package manager.", "info")
    installed, detail = install_tshark()
    if not installed:
        raise RuntimeError(detail)

    tshark_path = find_tshark_path()
    if not tshark_path:
        raise RuntimeError("The install step completed, but tshark is still not available on PATH.")

    version = get_tool_version_line(tshark_path, ["--version"])
    suffix = f" ({version})" if version else ""
    write_step(f"tshark is ready at {tshark_path}{suffix}", "success")
    return tshark_path


def read_pid_file() -> int | None:
    payload = SERVER_PID_PATH and SERVER_PID_PATH.is_file() and SERVER_PID_PATH.read_text(encoding="utf-8").strip()
    if not payload:
        return None
    try:
        return int(payload)
    except ValueError:
        return None


def write_pid_file(pid: int) -> None:
    SERVER_PID_PATH.parent.mkdir(parents=True, exist_ok=True)
    SERVER_PID_PATH.write_text(str(pid), encoding="utf-8")


def remove_pid_file() -> None:
    try:
        SERVER_PID_PATH.unlink(missing_ok=True)
    except Exception:
        pass


def is_occ_server_process(pid: int | None) -> bool:
    command_line = get_process_command_line(pid)
    if not command_line:
        return False
    return "occ_server.py" in command_line.lower()


def stop_known_server(force: bool) -> bool:
    if not force:
        return False

    pid = read_pid_file()
    if pid and process_exists(pid):
        if not is_occ_server_process(pid):
            raise RuntimeError(
                f"Refusing to stop PID {pid} from occ_server.pid because it is not the OCC server process. "
                "Delete utils/occ/data/occ_server.pid if it is stale, or stop the intended process manually."
            )
        write_step(f"Stopping the existing OCC server process {pid}.", "info")
        terminate_process(pid, force=True)
        wait_for_process_exit(pid, timeout_seconds=5.0)
        remove_pid_file()
        return True

    if is_occ_ready():
        raise RuntimeError(
            f"OCC is already running on port {PORT}, but it was not started by this cross-platform launcher. "
            "Restart it with the existing Windows launcher on Windows, or stop the current process manually before using --force-restart-server here."
        )

    return False


def start_server_process() -> subprocess.Popen:
    SERVER_STDOUT_LOG.write_text("", encoding="utf-8")
    SERVER_STDERR_LOG.write_text("", encoding="utf-8")

    stdout_handle = SERVER_STDOUT_LOG.open("w", encoding="utf-8")
    stderr_handle = SERVER_STDERR_LOG.open("w", encoding="utf-8")
    try:
        process = popen_command(
            [sys.executable, str(SERVER_SCRIPT)],
            cwd=str(ROOT),
            stdout=stdout_handle,
            stderr=stderr_handle,
            stdin=subprocess.DEVNULL,
            start_new_session=not sys.platform.startswith("win"),
        )
    finally:
        stdout_handle.close()
        stderr_handle.close()

    write_pid_file(process.pid)
    return process


def main() -> int:
    parser = argparse.ArgumentParser(description="Cross-platform OCC launcher.")
    parser.add_argument("--no-browser", action="store_true")
    parser.add_argument("--auto-install-tshark", action="store_true")
    parser.add_argument("--skip-tshark-check", action="store_true")
    parser.add_argument("--force-restart-server", action="store_true")
    args = parser.parse_args()

    print()
    print("Opcode Command Center")
    print("=====================")

    version_line = get_tool_version_line(sys.executable, ["--version"])
    suffix = f" ({version_line})" if version_line else ""
    write_step(f"Using Python runtime {sys.executable}{suffix}", "success")

    try:
        ensure_tshark(skip_check=args.skip_tshark_check, auto_install=args.auto_install_tshark)
        stop_known_server(args.force_restart_server)

        if is_occ_ready():
            write_step(f"OCC server is already running on port {PORT}.", "success")
        else:
            if is_port_open():
                raise RuntimeError(f"Port {PORT} is already in use by another process. Free the port before launching OCC.")

            write_step("Starting the local OCC bridge server.", "info")
            process = start_server_process()
            write_step(f"Started OCC server process {process.pid}. Waiting for {URL}...", "info")

            if not wait_for_occ_ready():
                if process.poll() is None:
                    try:
                        terminate_process(process.pid, force=True)
                    except Exception:
                        pass
                remove_pid_file()
                detail = read_log_tail(SERVER_STDERR_LOG, line_count=20)
                if detail:
                    raise RuntimeError(f"OCC did not become ready.\n{detail}")
                raise RuntimeError("OCC did not become ready within 15 seconds.")

            write_step("OCC server is ready.", "success")

        CAPTURES_ROOT.mkdir(parents=True, exist_ok=True)
        if args.no_browser:
            write_step("Browser launch skipped.", "info")
        else:
            write_step("Opening OCC in your default browser.", "info")
            open_browser(URL)

        print()
        write_step(f"OCC is available at {URL}", "success")
        print(f"    OCC root:     {ROOT}")
        print(f"    Captures:     {CAPTURES_ROOT}")
        print(f"    Server logs:  {SERVER_STDOUT_LOG}")
        print(f"                  {SERVER_STDERR_LOG}")
        return 0
    except Exception as exc:
        write_step(str(exc), "error")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
