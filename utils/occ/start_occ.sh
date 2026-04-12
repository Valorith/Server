#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if command -v python3 >/dev/null 2>&1; then
  PYTHON_BIN="python3"
elif command -v python >/dev/null 2>&1; then
  PYTHON_BIN="python"
else
  echo "[x] Python 3 is required to launch OCC. Install Python 3 and rerun start_occ.sh." >&2
  exit 1
fi

exec "$PYTHON_BIN" "$SCRIPT_DIR/scripts/start_occ.py" "$@"
