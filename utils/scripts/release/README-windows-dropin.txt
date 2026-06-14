EQEmu Windows x64 Drop-In Bundle
================================

This archive is a drop-in update bundle for an existing Windows EQEmu server
installation. It is not a standalone installer and is not expected to run from
an empty directory.

Use
---

1. Stop the running EQEmu server processes.
2. Back up the existing server binary directory.
3. Extract this archive over the existing server binary directory.
4. Keep your existing eqemu_config.json, login.json, quests, assets, maps,
   shared memory files, and database unchanged unless a release note says
   otherwise.
5. Start the server normally.

Runtime Prerequisites
---------------------

The target server must already be a working Windows EQEmu installation with:

- The supported Strawberry Perl 5.24 x64 runtime available in the server
  environment, either in PATH or in the binary directory.
- Existing server configuration and content directories.
- A reachable MariaDB/MySQL server and an updated EQEmu database.

The bundle includes EQEmu executables and the runtime DLLs produced by the
release build. It intentionally does not include the full Strawberry Perl
runtime or a complete server data/config installation.

Verification
------------

The release workflow smoke-tests this archive by overlaying it onto a temporary
installed-server fixture and launching the Windows server binaries with isolated
dummy configuration. The smoke test verifies that binaries load correctly and
exit cleanly on expected dummy database connection failure.
