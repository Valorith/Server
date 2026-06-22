#!/usr/bin/env python3
import argparse
import json
import re
from pathlib import Path


SEMVER_RE = re.compile(r"^v?(\d+)\.(\d+)\.(\d+)$")
MANIFEST_VERSION_RE = re.compile(r"^\s*\.version\s*=\s*(\d+)\s*,")

# Custom migrations remain opt-in through CUSTOM_BINARY_DATABASE_VERSION.
DATABASE_VERSION_MANIFESTS = (
    (
        "CURRENT_BINARY_DATABASE_VERSION",
        Path("common") / "database" / "database_update_manifest.h",
    ),
    (
        "CURRENT_BINARY_BOTS_DATABASE_VERSION",
        Path("common") / "database" / "database_update_manifest_bots.h",
    ),
)


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

    if new_text != text:
        path.write_text(new_text, encoding="utf-8")


def max_manifest_version(root: Path, manifest_path: Path) -> int:
    path = root / manifest_path
    versions = []

    for line in path.read_text(encoding="utf-8").splitlines():
        match = MANIFEST_VERSION_RE.match(line)
        if match:
            versions.append(int(match.group(1)))

    if not versions:
        raise SystemExit(f"No manifest versions found in {path}")

    return max(versions)


def get_define_value(root: Path, define_name: str) -> int:
    path = root / "common" / "version.h"
    pattern = re.compile(rf"^#define\s+{re.escape(define_name)}\s+(\d+)\s*$", re.MULTILINE)
    match = pattern.search(path.read_text(encoding="utf-8"))
    if not match:
        raise SystemExit(f"Could not find {define_name} in {path}")

    return int(match.group(1))


def set_define_value(root: Path, define_name: str, value: int) -> None:
    replace_once(
        root / "common" / "version.h",
        rf"(#define\s+{re.escape(define_name)}\s+)\d+",
        rf"\g<1>{value}",
    )


def expected_database_versions(root: Path) -> dict[str, int]:
    return {
        define_name: max_manifest_version(root, manifest_path)
        for define_name, manifest_path in DATABASE_VERSION_MANIFESTS
    }


def sync_database_versions(root: Path) -> None:
    for define_name, expected_version in expected_database_versions(root).items():
        set_define_value(root, define_name, expected_version)


def check_database_versions(root: Path) -> None:
    mismatches = []
    for define_name, expected_version in expected_database_versions(root).items():
        actual_version = get_define_value(root, define_name)
        if actual_version != expected_version:
            mismatches.append((define_name, actual_version, expected_version))

    if mismatches:
        for define_name, actual_version, expected_version in mismatches:
            print(
                f"{define_name} is {actual_version}, expected {expected_version} "
                "from the manifest"
            )
        raise SystemExit(1)


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

    sync_database_versions(root)


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

    sync_db_parser = subparsers.add_parser(
        "sync-db-versions",
        help="sync binary database versions from the manifest files",
    )
    sync_db_parser.add_argument("--root", default=".")

    check_db_parser = subparsers.add_parser(
        "check-db-versions",
        help="verify binary database versions match the manifest files",
    )
    check_db_parser.add_argument("--root", default=".")

    args = parser.parse_args()

    if args.command == "compute":
        print(compute_next(args.latest_tag, args.bump))
        return

    if args.command == "set":
        set_versions(Path(args.root).resolve(), args.version, args.channel)
        return

    if args.command == "sync-db-versions":
        sync_database_versions(Path(args.root).resolve())
        return

    if args.command == "check-db-versions":
        check_database_versions(Path(args.root).resolve())
        return

    raise SystemExit(f"Unsupported command [{args.command}]")


if __name__ == "__main__":
    main()
