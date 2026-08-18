#!/usr/bin/env bash
# Cursor Cloud Agent ONLY — not part of the normal EQEmu build/CI/installer.
# Per-boot: start MariaDB. See .cursor/README.md.
set -euo pipefail

if [[ "${EQEMU_ALLOW_CLOUD_AGENT_SCRIPTS:-}" != "1" ]]; then
	if [[ ! -d /opt/cursor || ! -d /exec-daemon || ! -f /workspace/CMakeLists.txt ]]; then
		echo "Refusing to run: .cursor/cloud-agent-start.sh is only for Cursor Cloud Agents." >&2
		echo "It is not used by CMake, CI, the Linux installer, or local builds." >&2
		echo "Override (dangerous): EQEMU_ALLOW_CLOUD_AGENT_SCRIPTS=1" >&2
		exit 1
	fi
fi

sudo service mariadb start
