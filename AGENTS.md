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

This section captures non-obvious, durable setup/run caveats for the Cloud Agent VM. The base image already has system packages (build-essential, cmake, ninja-build, ccache, autotools, MariaDB server/client, libperl-dev, uuid-dev, etc.) installed, the `peq` database seeded, and the project built. The startup update script only refreshes git submodules; everything else below is done once and persists in the environment snapshot.

### Compiler (important)
- The VM's default `cc`/`c++` alternatives are Clang, which selects a GCC-14 toolchain that has no `libstdc++` and fails to link (`cannot find -lstdc++`). This project (and vcpkg) must build with **GCC**. The alternatives have been repointed to `gcc`/`g++`. If a build ever picks Clang again, pass `-DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++` (and export `CC=gcc CXX=g++` so vcpkg's triplet build uses GCC too).

### Build
- C++ dependencies come from the `submodules/vcpkg` submodule (manifest `vcpkg.json`); the first configure compiles them from source into `build/vcpkg_installed` (cached in `/workspace/.vcpkg-binary-cache`). Use the canonical flags in `.github/workflows/build.yaml` (Linux job). Configure once, then `cmake --build build --parallel`. Binaries land in `build/bin/`. `BUILD.md` covers the manual build; `CMakePresets.json` has `linux-debug`/`linux-release` presets.

### Database
- MariaDB is not auto-started; run `sudo service mariadb start` at the beginning of a session. DB `peq` / user `peq`@`127.0.0.1` password `peqpass` (see `.devcontainer/base/eqemu_config.json`). Content is seeded from `https://db.eqemu.dev/latest`; re-seed via the `inject-mariadb` recipe in `.devcontainer/Makefile`.
- On `world` startup the in-code DB migrations may hit `Duplicate column`/already-applied errors because the public dump is newer than the binary's migration manifest. `world` then prompts `Would you like to skip this update? [y/n]` (60s timeout). Answer `y` to skip; this is expected, not a failure.

### Running the server stack
- Run every binary from `build/bin/` (configs `eqemu_config.json` and `login.json` live there, pointing at localhost). Bring-up order: `./shared_memory` (one-shot, serializes items+spells only in this version) → `./loginserver` → `./world` → `./zone`. Ports: loginserver 5998 (+web api 6000), world telnet 9000 / zone-TCP 9001 / http 9080, zones 7000-7400.
- `zone` aborts at startup with "Incompatible quest plugins detected" unless `quests`, `plugins`, `lua_modules`, and `mods` are symlinked into `build/bin/` from a clone of `github.com/ProjectEQ/projecteqquests` (the `prep` target in `.devcontainer/Makefile` does this). Maps (`github.com/EQEmu/maps`) are optional for a zone to boot.
- The `world` telnet console (`nc 127.0.0.1 9000`) treats localhost connections as admin (no login needed). Use it to drive the server, e.g. `zonestatus`, then `zonebootup <zone_server_id> <zone_short_name>` (e.g. `zonebootup 1 poknowledge`) to boot a live game zone.
- The devcontainer `make` targets (`make world`, etc.) are guarded by an `is-vscode` check and only run inside the VS Code devcontainer; run the binaries directly here instead.

### Tests / lint
- Unit tests: build with `-DEQEMU_BUILD_TESTS=ON`, then run `./build/bin/tests` (CI runs exactly this). The harness always exits 0; check the printed `Total: N tests, X% correct` line. There is no separate lint step.
