#!/usr/bin/env python3
import argparse
import json
import re
from pathlib import Path


SEMVER_RE = re.compile(r"^v?(\d+)\.(\d+)\.(\d+)$")


def parse_version(value: str) -> tuple[int, int, int]:
    match = SEMVER_RE.match(value.strip())
    if not match:
        raise SystemExit(f"Expected SemVer value like v1.2.3, got [{value}]")

    return tuple(int(part) for part in match.groups())


def format_version(version: tuple[int, int, int]) -> str:
    return ".".join(str(part) for part in version)


def compute_next(latest_tag: str, bump: str) -> str:
    major, minor, patch = parse_version(latest_tag)

    if bump == "major":
        major += 1
        minor = 0
        patch = 0
    elif bump == "minor":
        minor += 1
        patch = 0
    elif bump == "patch":
        patch += 1
    else:
        raise SystemExit(f"Unsupported bump [{bump}]")

    return format_version((major, minor, patch))


def replace_once(path: Path, pattern: str, replacement: str, *, flags: int = 0) -> None:
    text = path.read_text(encoding="utf-8")
    new_text, count = re.subn(pattern, replacement, text, count=1, flags=flags)
    if count != 1:
        raise SystemExit(f"Expected exactly one replacement in {path}")

    path.write_text(new_text, encoding="utf-8")


def set_package_version(root: Path, version: str) -> None:
    path = root / "package.json"
    data = json.loads(path.read_text(encoding="utf-8"))
    data["version"] = version
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")


def set_current_version(root: Path, current_version: str) -> None:
    replace_once(
        root / "common" / "version.h",
        r'(#define\s+CURRENT_VERSION\s+")[^"]+(")',
        rf"\g<1>{current_version}\2",
    )


def set_cmake_version(root: Path, version: str) -> None:
    replace_once(
        root / "CMakeLists.txt",
        r"(project\s*\(\s*EQEmu\s+VERSION\s+)\d+\.\d+\.\d+",
        rf"\g<1>{version}",
        flags=re.DOTALL,
    )


def set_versions(root: Path, version: str, channel: str) -> None:
    parse_version(version)

    if channel == "release":
        set_package_version(root, version)
        set_cmake_version(root, version)
        set_current_version(root, version)
    elif channel == "dev":
        set_current_version(root, f"{version}-dev")
    else:
        raise SystemExit(f"Unsupported channel [{channel}]")


def main() -> None:
    parser = argparse.ArgumentParser(description="EQEmu release version helper")
    subparsers = parser.add_subparsers(dest="command", required=True)

    compute_parser = subparsers.add_parser("compute", help="compute next release version")
    compute_parser.add_argument("--latest-tag", required=True)
    compute_parser.add_argument("--bump", choices=["patch", "minor", "major"], required=True)

    set_parser = subparsers.add_parser("set", help="update release version files")
    set_parser.add_argument("--root", default=".")
    set_parser.add_argument("--version", required=True)
    set_parser.add_argument("--channel", choices=["release", "dev"], required=True)

    args = parser.parse_args()

    if args.command == "compute":
        print(compute_next(args.latest_tag, args.bump))
        return

    if args.command == "set":
        set_versions(Path(args.root).resolve(), args.version, args.channel)
        return

    raise SystemExit(f"Unsupported command [{args.command}]")


if __name__ == "__main__":
    main()
