# AI Review Guidance

This repository is an EQEmulator server fork. Reviews should prioritize correctness and compatibility over cosmetic cleanup.

## Project Context

- The core server is C++20 built with CMake.
- The database layer targets MySQL/MariaDB.
- Zone gameplay, world state, login/session behavior, UCS, queryserv, and shared `common/` code have high regression risk.
- Quest and content behavior may involve both Perl and Lua bindings. Check API names, argument order, event semantics, and script-visible behavior carefully.
- SQL changes can affect long-lived production data. Treat migrations, defaults, repository generated code, and data updates as high impact.

## Review Priorities

- Flag crash risk, memory lifetime issues, data corruption, SQL injection, auth/session bugs, packet/protocol incompatibility, and cross-zone state regressions.
- For gameplay logic, look for behavioral changes that could alter NPC AI, combat, loot, spells, inventory, tasks, expeditions, raids, quests, zoning, or persistence.
- For shared `common/` helpers and repository code, consider blast radius across zone, world, loginserver, queryserv, UCS, tools, and tests.
- For database access, prefer parameterized queries and schema-compatible repository patterns. Be skeptical of raw SQL string concatenation.
- For generated repository headers, check whether the source schema or generator should change instead of hand-editing generated output.
- For tests, prefer focused regression coverage for edge cases and persisted behavior. Do not ask for broad test rewrites unless the changed code is high risk.

## What To Avoid

- Do not request style-only rewrites unless they hide a real correctness, safety, or maintainability problem.
- Do not recommend large modernizing refactors when a narrow fix preserves existing behavior.
- Do not assume generic MMO behavior when current EQEmu code or docs define the behavior differently.
- Do not require private server, deployment, credential, or environment-specific context for ordinary review comments.

## Useful Review Questions

- Could this break an existing client, packet version, or database row shape?
- Could this change behavior for existing quests or custom server content?
- Are null pointers, stale entity references, ownership boundaries, or async callbacks handled safely?
- Are database updates backward-compatible and safe for existing installs?
- Is there a targeted build, unit test, or reproducible scenario that should accompany the change?

## Cursor Cloud specific instructions

Optional guidance for [Cursor Cloud Agents](https://cursor.com/docs/cloud-agent) only. It does **not** change CMake, CI, the Linux installer, or local builds for other contributors. Helpers live under `.cursor/` and run only when a Cursor environment Install/Start command invokes them (see `.cursor/README.md`).

Recommended Cursor dashboard commands: Install `bash .cursor/cloud-agent-install.sh`, Start `bash .cursor/cloud-agent-start.sh`. A submodule-only install is insufficient for this codebase (needs compilers, binaries, and a seeded DB).

### Compiler (important)
- Default `cc`/`c++` on the base image may be Clang, which selects a GCC-14 toolchain with no usable `libstdc++` (`cannot find -lstdc++`). Always build with **GCC**. The install script forces the alternatives to `gcc`/`g++`. If a build still picks Clang, pass `-DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++` and `CC=gcc CXX=g++` for vcpkg.

### Build
- C++ deps come from `submodules/vcpkg` (`vcpkg.json`). First configure compiles them into `build/vcpkg_installed` (binary cache under `.vcpkg-binary-cache`). Canonical flags match `.github/workflows/build.yaml` (Linux job). Binaries land in `build/bin/`. See also `BUILD.md` / `CMakePresets.json`.

### Database
- Start script starts MariaDB; if you are not using it, run `sudo service mariadb start` yourself. DB `peq` / user `peq` password `peqpass` (see `.devcontainer/base/eqemu_config.json`). Content is seeded from `https://db.eqemu.dev/latest` by the install script (or `inject-mariadb` in `.devcontainer/Makefile`).
- On `world` startup, in-code migrations may hit `Duplicate column` because the public dump can be ahead of the binary manifest. `world` prompts `Would you like to skip this update? [y/n]` (60s). Answer `y`; this is expected.

### Running the server stack
- Run binaries from `build/bin/` (`eqemu_config.json` / `login.json` there, localhost). Bring-up: `./shared_memory` (one-shot; items+spells only in this tree) → `./loginserver` → `./world` → `./zone`. Ports: login 5998 (+API 6000), world telnet 9000 / zone-TCP 9001 / http 9080, zones 7000–7400.
- `zone` aborts without quest plugins (`CheckHandin`). Install links `quests`/`plugins`/`lua_modules`/`mods` from ProjectEQ quests. Maps are optional for boot.
- World telnet (`nc 127.0.0.1 9000`) treats localhost as admin. Useful: `zonestatus`, `zonebootup <id> <short_name>` (e.g. `zonebootup 1 poknowledge`).
- Devcontainer `make` targets require VS Code (`is-vscode`); run binaries directly in Cloud Agents.

### Tests / lint
- `./build/bin/tests` (build with `-DEQEMU_BUILD_TESTS=ON`; CI runs this). Harness exits 0; check `Total: N tests, X% correct`. No separate lint step.
