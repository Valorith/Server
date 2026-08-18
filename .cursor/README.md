# Cursor Cloud Agent helpers (optional)

These files are **only** for [Cursor Cloud Agents](https://cursor.com/docs/cloud-agent). They are **not** part of the normal EQEmu build, CI, Linux installer, or local development workflow.

| File | When it runs |
| --- | --- |
| `cloud-agent-install.sh` | Only if a Cursor environment's **Install** command invokes it |
| `cloud-agent-start.sh` | Only if a Cursor environment's **Start** command invokes it |

Nothing in CMake, GitHub Actions, or the server binaries references these scripts. Contributors who never use Cursor Cloud can ignore this directory.

Scripts refuse to run outside a Cursor Cloud VM (unless `EQEMU_ALLOW_CLOUD_AGENT_SCRIPTS=1` is set) so accidental local execution cannot change your default compiler, apt packages, or databases.

There is intentionally **no** committed `environment.json` here, so this repo does not override personal/team Cursor environment settings for other users.
