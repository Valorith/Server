#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 3 ]; then
  echo "Usage: $0 <version> <tag> <commit-sha>" >&2
  exit 2
fi

VERSION="$1"
TAG="$2"
COMMIT_SHA="$3"

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
BIN_DIR="${EQEMU_RELEASE_BIN_DIR:-${ROOT_DIR}/build/bin}"
DIST_DIR="${EQEMU_RELEASE_DIST_DIR:-${ROOT_DIR}/dist}"
PACKAGE_NAME="eqemu-server-linux-x64-${TAG}"
STAGE_DIR="${DIST_DIR}/${PACKAGE_NAME}"
ZIP_PATH="${DIST_DIR}/${PACKAGE_NAME}.zip"

REQUIRED_BINARIES=(
  world
  zone
  ucs
  queryserv
  eqlaunch
  shared_memory
  loginserver
  import_client_files
  export_client_files
)

copy_unique() {
  local source_path="$1"
  local target_name
  target_name="$(basename "$source_path")"
  if [ -e "${STAGE_DIR}/${target_name}" ]; then
    return 0
  fi

  cp -aL "$source_path" "${STAGE_DIR}/${target_name}"
}

require_tool() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "Required tool [$1] was not found." >&2
    exit 1
  fi
}

require_tool ldd
require_tool python3
require_tool zip

rm -rf "$STAGE_DIR" "$ZIP_PATH"
mkdir -p "$STAGE_DIR"

for binary in "${REQUIRED_BINARIES[@]}"; do
  if [ ! -f "${BIN_DIR}/${binary}" ]; then
    echo "Required binary is missing: ${BIN_DIR}/${binary}" >&2
    exit 1
  fi
done

for binary in "${REQUIRED_BINARIES[@]}"; do
  copy_unique "${BIN_DIR}/${binary}"
done

for binary in "${REQUIRED_BINARIES[@]}"; do
  ldd_output="$(LD_LIBRARY_PATH="$STAGE_DIR" ldd "${STAGE_DIR}/${binary}" || true)"
  echo "$ldd_output"
  if printf '%s\n' "$ldd_output" | grep -q "not found"; then
    echo "Missing Linux runtime dependency for ${binary}." >&2
    exit 1
  fi
done

python3 - "$STAGE_DIR" "$VERSION" "$TAG" "$COMMIT_SHA" <<'PY'
import hashlib
import json
import sys
from datetime import datetime, timezone
from pathlib import Path

stage_dir = Path(sys.argv[1])
version = sys.argv[2]
tag = sys.argv[3]
commit_sha = sys.argv[4]

files = []
for path in sorted(stage_dir.rglob("*")):
    if not path.is_file() or path.name == "release-manifest.json":
        continue

    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    files.append({
        "path": path.relative_to(stage_dir).as_posix(),
        "size": path.stat().st_size,
        "sha256": digest,
    })

manifest = {
    "version": version,
    "tag": tag,
    "commit_sha": commit_sha,
    "platform": "linux-x64",
    "generated_at": datetime.now(timezone.utc).isoformat(),
    "files": files,
}

(stage_dir / "release-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
PY

(
  cd "$DIST_DIR"
  zip -r "$(basename "$ZIP_PATH")" "$PACKAGE_NAME"
)

if [ ! -f "$ZIP_PATH" ]; then
  echo "Failed to create ${ZIP_PATH}" >&2
  exit 1
fi

echo "Created ${ZIP_PATH}"
