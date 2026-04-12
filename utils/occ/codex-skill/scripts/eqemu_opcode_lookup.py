#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import os
import re
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

SEARCH_ROOTS = ("common", "world", "zone", "loginserver", "ucs", "queryserv", "eqlaunch", "utils")
SEARCH_SUFFIXES = {".cpp", ".h", ".conf", ".py", ".cs"}
SKIP_DIRS = {".git", ".vs", "build-akkstack-linux", "build-codex", "submodules", "dependencies"}
PATCH_LINE_RE = re.compile(r"^(OP_[A-Za-z0-9_]+)=0x([0-9A-Fa-f]{4})")
HEX_OPCODE_RE = re.compile(r"0x([0-9A-Fa-f]{1,4})\b")
OPCODE_NAME_RE = re.compile(r"\b(OP_[A-Za-z0-9_]+)\b")
SHOWEQ_OPCODE_RE = re.compile(
    r"OpCode:\s*(?P<label>[A-Za-z0-9_]+)?(?:::(?P<byte1>[0-9A-Fa-f]{2})\s*[, ]\s*(?P<byte2>[0-9A-Fa-f]{2})(?:\s*[, ]\s*(?P<byte3>[0-9A-Fa-f]{2}))?)",
    re.IGNORECASE,
)
BYTE_TOKEN_RE = re.compile(r"(?<![0-9A-Fa-f])([0-9A-Fa-f]{2})(?![0-9A-Fa-f])")


def normalize_client(value: str) -> str:
    key = value.strip().lower()
    if key not in CLIENT_ALIASES:
        raise ValueError(f"Unsupported client '{value}'. Expected one of: Titanium, SoF, SoD, UF, RoF, RoF2.")
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

    env_root = os.environ.get("EQEMU_REPO_ROOT")
    if env_root:
        candidates.append(Path(env_root).expanduser().resolve())

    cwd = Path.cwd().resolve()
    candidates.extend([cwd, *cwd.parents])

    default_root = Path(r"C:\AkkStack\code")
    if default_root not in candidates:
        candidates.append(default_root)

    seen: set[Path] = set()
    for candidate in candidates:
        if candidate in seen:
            continue
        seen.add(candidate)
        if looks_like_repo_root(candidate):
            return candidate

    raise FileNotFoundError(
        "Could not locate the EQEmu repo root. Run from the repo, set EQEMU_REPO_ROOT, or pass --repo-root."
    )


def patch_path(repo_root: Path, client_key: str) -> Path:
    path = repo_root / "utils" / "patches" / CLIENT_PATCHES[client_key]
    if not path.is_file():
        raise FileNotFoundError(f"Patch file not found: {path}")
    return path


def load_patch_maps(path: Path) -> tuple[dict[str, int], dict[int, list[str]]]:
    by_name: dict[str, int] = {}
    by_value: dict[int, list[str]] = {}

    with path.open("r", encoding="utf-8", errors="ignore") as handle:
        for line in handle:
            match = PATCH_LINE_RE.match(line.strip())
            if not match:
                continue
            name = match.group(1)
            value = int(match.group(2), 16)
            by_name[name] = value
            by_value.setdefault(value, []).append(name)

    return by_name, by_value


def parse_opcode_value(text: str) -> int:
    match = HEX_OPCODE_RE.search(text.strip())
    if match:
        return int(match.group(1), 16)

    stripped = text.strip()
    if re.fullmatch(r"[0-9A-Fa-f]{1,4}", stripped):
        return int(stripped, 16)

    raise ValueError(f"Could not parse opcode value from '{text}'.")


def parse_raw_bytes(text: str) -> int:
    tokens = BYTE_TOKEN_RE.findall(text)
    if len(tokens) not in (2, 3):
        raise ValueError(
            f"Expected 2 or 3 hex byte tokens, got {len(tokens)} from '{text}'. Example inputs: '13 72' or '00,00,12'."
        )

    values = [int(token, 16) for token in tokens]
    if len(values) == 3:
        if values[0] != 0:
            raise ValueError(
                f"Three-byte form must start with 00. Got '{text}'. This form is only used for opcodes with a low byte of 00."
            )
        return values[1] | (values[2] << 8)

    return values[0] | (values[1] << 8)


def format_opcode(opcode: int | None) -> str:
    return "" if opcode is None else f"0x{opcode:04x}"


def format_bytes(opcode: int | None) -> str:
    if opcode is None:
        return ""
    if (opcode & 0x00FF) == 0:
        return f"00,{opcode & 0x00FF:02x},{(opcode >> 8) & 0x00FF:02x}"
    return f"{opcode & 0x00FF:02x},{(opcode >> 8) & 0x00FF:02x}"


def search_code_refs(repo_root: Path, opcode_name: str, limit: int) -> list[dict[str, object]]:
    results: list[dict[str, object]] = []

    for root_name in SEARCH_ROOTS:
        root = repo_root / root_name
        if not root.exists():
            continue

        for current_root, dirnames, filenames in os.walk(root):
            dirnames[:] = [name for name in dirnames if name not in SKIP_DIRS]
            current_path = Path(current_root)

            for filename in filenames:
                file_path = current_path / filename
                if file_path.suffix.lower() not in SEARCH_SUFFIXES:
                    continue

                try:
                    with file_path.open("r", encoding="utf-8", errors="ignore") as handle:
                        for line_number, line in enumerate(handle, start=1):
                            if opcode_name not in line:
                                continue
                            results.append(
                                {
                                    "path": str(file_path.relative_to(repo_root)),
                                    "line": line_number,
                                    "text": line.strip(),
                                }
                            )
                            if len(results) >= limit:
                                return results
                except OSError:
                    continue

    return results


def resolve_by_opcode(
    opcode: int,
    client_key: str,
    patch_file: Path,
    by_value: dict[int, list[str]],
    repo_root: Path,
    with_code_refs: bool,
    code_ref_limit: int,
    input_kind: str,
    input_value: str,
    extra: dict[str, str] | None = None,
) -> dict[str, object]:
    names = by_value.get(opcode, [])
    code_refs: list[dict[str, object]] = []
    if with_code_refs and names:
        code_refs = search_code_refs(repo_root, names[0], code_ref_limit)

    result = {
        "client": display_client(client_key),
        "patch_file": str(patch_file),
        "input_kind": input_kind,
        "input_value": input_value,
        "opcode": format_opcode(opcode),
        "raw_bytes": format_bytes(opcode),
        "names": names,
        "status": "ok" if names else "unknown",
        "code_refs": code_refs,
    }

    if extra:
        result.update(extra)

    return result


def resolve_by_name(
    opcode_name: str,
    client_key: str,
    patch_file: Path,
    by_name: dict[str, int],
    repo_root: Path,
    with_code_refs: bool,
    code_ref_limit: int,
    input_kind: str,
    input_value: str,
    extra: dict[str, str] | None = None,
) -> dict[str, object]:
    opcode = by_name.get(opcode_name)
    code_refs: list[dict[str, object]] = []
    if with_code_refs:
        code_refs = search_code_refs(repo_root, opcode_name, code_ref_limit)

    result = {
        "client": display_client(client_key),
        "patch_file": str(patch_file),
        "input_kind": input_kind,
        "input_value": input_value,
        "opcode": format_opcode(opcode),
        "raw_bytes": format_bytes(opcode),
        "names": [opcode_name] if opcode is not None else [],
        "status": "ok" if opcode is not None else "unknown",
        "code_refs": code_refs,
    }

    if extra:
        result.update(extra)

    return result


def print_text_result(result: dict[str, object]) -> None:
    print(f"Client: {result['client']}")
    print(f"Patch: {result['patch_file']}")
    print(f"Input: {result['input_kind']} {result['input_value']}")
    print(f"Opcode: {result['opcode'] or '<unknown>'}")
    print(f"Raw Bytes: {result['raw_bytes'] or '<unknown>'}")
    print(f"Names: {', '.join(result['names']) if result['names'] else '<unknown>'}")
    if "showeq_label" in result:
        print(f"ShowEQ Label: {result['showeq_label']}")
    if "line_number" in result:
        print(f"Line: {result['line_number']}")

    code_refs = result.get("code_refs", [])
    if code_refs:
        print("Code Refs:")
        for ref in code_refs:
            print(f"  {ref['path']}:{ref['line']} {ref['text']}")


def output_results(results: list[dict[str, object]], output_format: str) -> None:
    if output_format == "json":
        print(json.dumps(results if len(results) != 1 else results[0], indent=2))
        return

    if len(results) == 1:
        print_text_result(results[0])
        return

    print("line\tstatus\tinput\topcode\traw_bytes\tnames\tshoweq_label")
    for result in results:
        names = ",".join(result["names"])
        showeq_label = result.get("showeq_label", "")
        print(
            f"{result.get('line_number', '')}\t{result['status']}\t{result['input_value']}\t{result['opcode']}\t"
            f"{result['raw_bytes']}\t{names}\t{showeq_label}"
        )


def resolve_line(
    line: str,
    line_number: int,
    mode: str,
    client_key: str,
    patch_file: Path,
    by_name: dict[str, int],
    by_value: dict[int, list[str]],
    repo_root: Path,
    with_code_refs: bool,
    code_ref_limit: int,
) -> dict[str, object]:
    stripped = line.strip()

    if not stripped or stripped.startswith("#"):
        return {
            "client": display_client(client_key),
            "patch_file": str(patch_file),
            "input_kind": "skip",
            "input_value": stripped,
            "opcode": "",
            "raw_bytes": "",
            "names": [],
            "status": "skip",
            "code_refs": [],
            "line_number": line_number,
        }

    detected_mode = mode
    showeq_label: str | None = None

    if mode == "auto":
        if SHOWEQ_OPCODE_RE.search(stripped):
            detected_mode = "showeq"
        elif OPCODE_NAME_RE.search(stripped):
            detected_mode = "name"
        elif HEX_OPCODE_RE.search(stripped):
            detected_mode = "opcode"
        else:
            detected_mode = "raw-bytes"

    if detected_mode == "showeq":
        match = SHOWEQ_OPCODE_RE.search(stripped)
        if not match:
            raise ValueError(f"Line {line_number}: not a ShowEQ opcode line.")
        showeq_label = match.group("label") or ""
        byte_text = ",".join(part for part in (match.group("byte1"), match.group("byte2"), match.group("byte3")) if part)
        opcode = parse_raw_bytes(byte_text)
        return resolve_by_opcode(
            opcode,
            client_key,
            patch_file,
            by_value,
            repo_root,
            with_code_refs,
            code_ref_limit,
            "showeq",
            stripped,
            {"line_number": line_number, "showeq_label": showeq_label},
        )

    if detected_mode == "name":
        match = OPCODE_NAME_RE.search(stripped)
        if not match:
            raise ValueError(f"Line {line_number}: missing OP_* token.")
        return resolve_by_name(
            match.group(1),
            client_key,
            patch_file,
            by_name,
            repo_root,
            with_code_refs,
            code_ref_limit,
            "name",
            stripped,
            {"line_number": line_number},
        )

    if detected_mode == "opcode":
        opcode = parse_opcode_value(stripped)
        return resolve_by_opcode(
            opcode,
            client_key,
            patch_file,
            by_value,
            repo_root,
            with_code_refs,
            code_ref_limit,
            "opcode",
            stripped,
            {"line_number": line_number},
        )

    opcode = parse_raw_bytes(stripped)
    return resolve_by_opcode(
        opcode,
        client_key,
        patch_file,
        by_value,
        repo_root,
        with_code_refs,
        code_ref_limit,
        "raw-bytes",
        stripped,
        {"line_number": line_number},
    )


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Resolve EQEmu opcodes from raw values, raw bytes, opcode names, or ShowEQ text exports."
    )
    parser.add_argument("--repo-root", help="Path to the EQEmu repo root. Defaults to cwd parents, EQEMU_REPO_ROOT, or C:\\AkkStack\\code.")
    parser.add_argument("--format", choices=("text", "json"), default="text")

    subparsers = parser.add_subparsers(dest="command", required=True)

    lookup = subparsers.add_parser("lookup", help="Resolve a single opcode.")
    lookup.add_argument("--client", required=True, help="Titanium, SoF, SoD, UF, RoF, or RoF2.")
    lookup.add_argument("--with-code-refs", action="store_true", help="Search the local codebase for the resolved opcode name.")
    lookup.add_argument("--code-ref-limit", type=int, default=8)
    lookup_group = lookup.add_mutually_exclusive_group(required=True)
    lookup_group.add_argument("--opcode", help="Raw opcode such as 0x7213.")
    lookup_group.add_argument("--raw-bytes", help="Little-endian opcode bytes such as '13,72' or '00,00,12'.")
    lookup_group.add_argument("--name", help="Opcode name such as OP_ZoneEntry.")

    annotate = subparsers.add_parser("annotate", help="Annotate a line-based text file or ShowEQ export.")
    annotate.add_argument("--client", required=True, help="Titanium, SoF, SoD, UF, RoF, or RoF2.")
    annotate.add_argument("--input", required=True, help="Path to a text file to annotate.")
    annotate.add_argument("--mode", choices=("auto", "showeq", "opcode", "raw-bytes", "name"), default="auto")
    annotate.add_argument("--with-code-refs", action="store_true", help="Search the local codebase for each resolved opcode name.")
    annotate.add_argument("--code-ref-limit", type=int, default=4)

    return parser


def run_lookup(args: argparse.Namespace, repo_root: Path, patch_file: Path, by_name: dict[str, int], by_value: dict[int, list[str]]) -> dict[str, object]:
    client_key = normalize_client(args.client)
    if args.opcode:
        opcode = parse_opcode_value(args.opcode)
        return resolve_by_opcode(
            opcode,
            client_key,
            patch_file,
            by_value,
            repo_root,
            args.with_code_refs,
            args.code_ref_limit,
            "opcode",
            args.opcode,
        )
    if args.raw_bytes:
        opcode = parse_raw_bytes(args.raw_bytes)
        return resolve_by_opcode(
            opcode,
            client_key,
            patch_file,
            by_value,
            repo_root,
            args.with_code_refs,
            args.code_ref_limit,
            "raw-bytes",
            args.raw_bytes,
        )
    return resolve_by_name(
        args.name.strip(),
        client_key,
        patch_file,
        by_name,
        repo_root,
        args.with_code_refs,
        args.code_ref_limit,
        "name",
        args.name,
    )


def run_annotate(
    args: argparse.Namespace,
    repo_root: Path,
    patch_file: Path,
    by_name: dict[str, int],
    by_value: dict[int, list[str]],
) -> list[dict[str, object]]:
    client_key = normalize_client(args.client)
    input_path = Path(args.input).expanduser().resolve()
    if not input_path.is_file():
        raise FileNotFoundError(f"Input file not found: {input_path}")

    results: list[dict[str, object]] = []
    with input_path.open("r", encoding="utf-8", errors="ignore") as handle:
        for line_number, line in enumerate(handle, start=1):
            try:
                result = resolve_line(
                    line,
                    line_number,
                    args.mode,
                    client_key,
                    patch_file,
                    by_name,
                    by_value,
                    repo_root,
                    args.with_code_refs,
                    args.code_ref_limit,
                )
            except ValueError as exc:
                result = {
                    "client": display_client(client_key),
                    "patch_file": str(patch_file),
                    "input_kind": args.mode,
                    "input_value": line.strip(),
                    "opcode": "",
                    "raw_bytes": "",
                    "names": [],
                    "status": f"error: {exc}",
                    "code_refs": [],
                    "line_number": line_number,
                }
            if result["status"] != "skip":
                results.append(result)

    return results


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()

    try:
        repo_root = find_repo_root(args.repo_root)
        client_key = normalize_client(args.client)
        patch_file = patch_path(repo_root, client_key)
        by_name, by_value = load_patch_maps(patch_file)

        if args.command == "lookup":
            results = [run_lookup(args, repo_root, patch_file, by_name, by_value)]
        else:
            results = run_annotate(args, repo_root, patch_file, by_name, by_value)

        output_results(results, args.format)
        return 0
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
