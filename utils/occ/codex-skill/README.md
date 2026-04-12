# OCC Codex Skill Snapshot

This folder contains a repo-local copy of the OCC Codex skill as an optional tool for Codex users.

Purpose:
- keep the OCC workflow documented inside the repo
- make the skill easy to inspect, copy, or install without depending on a private `$CODEX_HOME` path
- provide repo-local script paths that match the OCC launcher and bridge now shipped in `utils/occ`

Notes:
- OCC itself does not depend on this folder at runtime
- the live session commands in this snapshot use the main OCC session helper from `utils/occ/scripts`
- if you want to install this as a real Codex skill, copy this folder into your Codex skills directory and keep the repo at `C:\AkkStack\code`, or adjust the absolute paths in `SKILL.md`
