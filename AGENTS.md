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
