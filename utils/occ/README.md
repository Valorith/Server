# Opcode Command Center (OCC)

OCC is a local web utility for cooperative RoF2 opcode research.

It is designed for the workflow where:
- a live `tshark` capture session is running
- the user performs actions in the EverQuest client
- OCC tracks live packet activity, candidate packet families, suspected opcodes, markers, and confirmation work

OCC is not just a registry. It is the main working surface for:
- running and monitoring capture sessions
- watching live packet activity
- creating custom packet-family candidates
- tracking repeated triggers
- recording opcode hypotheses
- promoting candidates into confirmed working mappings

## Launching OCC

Windows:

```powershell
C:\AkkStack\code\utils\occ\start_occ.ps1
```

Linux or macOS:

```bash
/path/to/repo/utils/occ/start_occ.sh
```

You can also call the shared Python launcher directly:

```bash
python3 /path/to/repo/utils/occ/scripts/start_occ.py
```

On first run, the launcher:
- checks for Python 3
- checks for `tshark`
- offers guided `tshark` installation help if `tshark` is missing
- starts the OCC bridge server
- opens OCC in your default browser

OCC then opens:

```text
http://127.0.0.1:8765/
```

Notes:
- OCC defaults new capture sessions to `Ethernet` when available.
- The page talks to the local OCC bridge in [C:\AkkStack\code\utils\occ\scripts\occ_server.py](C:/AkkStack/code/utils/occ/scripts/occ_server.py).
- Capture data is stored under `C:\AkkStack\.codex\captures\sessions\`.
- An optional repo-local Codex skill snapshot now ships in [C:\AkkStack\code\utils\occ\codex-skill](C:/AkkStack/code/utils/occ/codex-skill/README.md).
- Windows keeps the existing PowerShell launcher and session helper; Linux/macOS use the shared Python launcher and session helper.

Useful launcher options:

```powershell
C:\AkkStack\code\utils\occ\start_occ.ps1 -NoBrowser
C:\AkkStack\code\utils\occ\start_occ.ps1 -AutoInstallTshark
```

```bash
/path/to/repo/utils/occ/start_occ.sh --no-browser
/path/to/repo/utils/occ/start_occ.sh --auto-install-tshark
```

If the Windows Wireshark installer appears, keep the `TShark` feature enabled and allow `Npcap` if prompted. On Linux/macOS, follow the launcher guidance for your package manager and make sure your user has permission to capture packets.

## Main Areas

### Capture Session

Use this section to control the active capture session.

Fields:
- `Session`: the capture session name
- `Interface`: valid local capture interfaces from `tshark -D`
- `Capture Filter`: normally `udp`
- `Duration (sec)`: optional timed capture

Buttons:
- `Start Capture`: starts a new session
- `Stop Capture`: stops the running session
- `Restart`: restarts the current running session using the same name, but wipes the previous session data so it is fresh
- folder icon: opens `C:\AkkStack\.codex\captures\sessions`
- heartbeat icon: opens the live monitor modal

Important behavior:
- starting a fresh session auto-seeds a unique session name
- restarting keeps the same session name but clears prior `.pcapng`, `markers.jsonl`, detections, and logs for that session
- `Open live monitor` is only useful while a session is active

### Interaction Markers

Use markers to tie a specific client action to nearby packet activity.

Recommended pattern:
1. Start a capture.
2. Perform exactly one client action.
3. Immediately add a marker.
4. Repeat the exact same action several times.

Why markers matter:
- they make timing evidence auditable
- they help separate signal from background traffic
- they support the candidate workflow when comparing repeated actions

The section shows:
- the current session’s markers
- pagination for the marker list
- a reset action to clear markers for the current session

Clicking a marker opens its modal so you can inspect or annotate it.

### Opcode Registry

This is the main registry of:
- seeded RoF2 reference entries
- EQEmu-backed entries
- custom user-created entries

Key columns:
- `Alert`: whether OCC should alert when the row matches live traffic
- `Opcode`: working RoF2 opcode for the row
- `Count`: how many times the row matched the current session
- `Name`: primary display name
- `EQEmu`: EQEmu-side `OP_*` name when known
- `Status`: lifecycle state
- `Source`: reference, EQEmu, or custom
- `Notes`: important candidate/reference evidence

Behavior:
- header click sorting is supported
- `Count` becomes the default sort when a capture starts
- rows briefly pulse when their session count increases
- clicking a row opens the inspector modal

## Inspector Modal

The inspector is where you manage a selected registry row.

For seeded/reference rows:
- identity fields are read-only
- classification, notes, workflow, and alerts remain editable

For custom rows:
- `RoF2 Opcode`
- `Reference Name`
- `EQEmu Name`
- `Test Opcode`

are editable directly in the inspector.

Use the fields like this:
- `Test Opcode`: your current hypothesis
- `RoF2 Opcode`: the promoted working opcode once the hypothesis is strong enough
- `Reference Name`: your main label for the candidate
- `EQEmu Name`: optional `OP_*` style label if you want to track a suspected semantic mapping

Other controls:
- `Status`
- `Confidence`
- `Alert on live detection`
- `Tags`
- cooperative notes
- workflow launcher

## Creating Custom Entries

Use `Add Custom Entry` when you have identified a packet family or suspect mapping that is not already represented in the registry.

Fields:
- `Opcode (optional)`: only fill this if you already have a serious candidate
- `Name`: candidate label
- `Packet Signature`: representative packet prefix evidence
- `Packet Family`: stable family identity from the live feed
- `Confidence`
- `Notes`

Best practice:
- prefer `Packet Family` over a one-off packet signature when the packet bytes drift between triggers
- keep `Opcode` blank until you have a real hypothesis
- put the early guess in `Test Opcode`, not `RoF2 Opcode`

The live feed context menu can also seed a custom entry directly from a packet family. That is usually the best path because it uses OCC’s existing family identity instead of relying on a manually copied prefix.

## Live Monitor

Open the live monitor from the heartbeat icon in the Capture Session section.

The monitor has tabs:
- `Live Feed`
- `Suppressed`

### Live Feed

The live feed shows grouped live capture activity.

Feed behavior:
- repeated similar packets are grouped together
- each entry shows `Nx in feed`
- new items pulse briefly when they arrive
- the feed is optimized for performance by grouping and limiting rendered entries

Common controls:
- `View`
- free-text filter
- `Unknown only`
- `Count Limit`
- `Clear All`

View modes:
- `All traffic`: everything that passes the current lane filters
- `EQ-like only`: traffic that OCC considers likely to be real EQ traffic
- `Opcode candidates`: items with opcode candidates
- `Flagged matches`: only rows that match armed alerts

Useful controls:
- `Unknown only`: hide packets that already resolve to known registry entries
- `Count Limit`: hide very repetitive feed groups above a threshold
- `Clear All`: flushes the current session’s live detections/activity from `detections.json` so the feed starts fresh from that point forward

Each live feed row supports:
- copy packet bytes
- rename packet/opcode label
- flag for live alerts
- suppress from live feed
- open linked registry row
- create custom registry entry

### Suppressed

This tab shows the current suppression rules.

Suppression behavior:
- suppressions are deduplicated
- the same rule can only exist once
- suppressed rows show packet snapshot data
- suppressing a grouped live row removes the grouped row from the live feed, not just one underlying packet

Use this tab to:
- review suppressed packet families/opcodes
- see why they were suppressed
- remove suppression rules

## Alerts

Any registry row can be armed for alerting.

When the row matches live activity, OCC can:
- show an alert modal
- play an audible tone
- pulse/highlight the relevant registry row

Alerts work for:
- known opcode rows
- custom packet-family rows
- packet-signature-backed rows
- custom alias-driven rows that consistently match the same live packet family

This is useful when you are waiting for a specific packet family while testing a client action repeatedly.

## Candidate Workflow

The candidate workflow is the structured research path for unknown mappings.

Stages:
1. `Candidate Packet`
2. `Repeatable Trigger`
3. `Isolated Capture`
4. `Opcode Hypothesis`
5. `Confirmed Mapping`

The workflow modal provides:
- current session count
- linked marker count
- signal identity type
- alert state
- completion checklist
- automated packet analysis
- reference-guided next steps

### What the workflow automates

For the selected row, OCC tries to:
- collect matching live packets
- determine the dominant route
- identify the main decode source
- identify likely request/response pairs
- compare repeated payload bytes
- build a stable-byte mask
- detect stable raw 16-bit windows
- suggest a provisional opcode hypothesis when possible

If a strong candidate exists, OCC can:
- record it into `Test Opcode`
- suggest moving the row into `Opcode Hypothesis`
- let you promote it later into `RoF2 Opcode`

### How to use the workflow correctly

If you have only proven repeatability:
- keep the row as a packet-family candidate
- do not prematurely write the main opcode

When OCC gives you a serious candidate:
- record it into `Test Opcode`
- compare it against repeated captures
- compare it against neighboring packets around markers
- check ShowEQ/EQEmu/code references

Only once the evidence is strong:
- promote it into `RoF2 Opcode`
- move status/confidence upward

## Recommended Research Pattern

For unknown opcode work, this is the preferred sequence:

1. Start a clean capture on `Ethernet`.
2. Perform one target action only.
3. Add a marker immediately.
4. Repeat that exact action several times.
5. Watch the live feed for the repeated packet family.
6. Alias the packet family if it is clearly tied to the action.
7. Create a custom registry entry from the live feed.
8. Arm alerts if you want instant confirmation on repeated triggers.
9. Use the workflow modal to inspect repeated-packet analysis.
10. Record the best hypothesis into `Test Opcode`.
11. Validate it against references, neighboring packets, and code.
12. Promote to `RoF2 Opcode` only when the mapping is strong enough.

## ShowEQ / Code / Lua Guidance

OCC helps automate the early and middle stages, but some mappings still require external validation.

Use the workflow’s `Reference-Guided Path` for the next steps when needed:
- ShowEQ patch/date cross-check
- `worldopcodes.xml` / `zoneopcodes.xml`
- client opcode-block comparison
- emulator-side Lua packet replay for confirmation

Important limitation:
- a repeated packet family is not automatically the inner application opcode
- some traffic is still protected, compressed, combined, oversized, or otherwise wrapped
- that means a stable family can be real and useful even before the final inner opcode is proven

## State Management

Open the settings drawer from the gear icon in the header.

Available actions:
- `Export State`
- `Import State`
- `Reset Local State`

These actions affect OCC browser state such as:
- custom entries
- notes
- aliases
- alerts
- suppressions
- local workflow progress

They do not delete the seeded reference data.

## Local Files

Important OCC files:
- [C:\AkkStack\code\utils\occ\index.html](C:/AkkStack/code/utils/occ/index.html)
- [C:\AkkStack\code\utils\occ\app.js](C:/AkkStack/code/utils/occ/app.js)
- [C:\AkkStack\code\utils\occ\app.css](C:/AkkStack/code/utils/occ/app.css)
- [C:\AkkStack\code\utils\occ\scripts\occ_server.py](C:/AkkStack/code/utils/occ/scripts/occ_server.py)
- [C:\AkkStack\code\utils\occ\scripts\occ_runtime.py](C:/AkkStack/code/utils/occ/scripts/occ_runtime.py)
- [C:\AkkStack\code\utils\occ\scripts\occ_session.py](C:/AkkStack/code/utils/occ/scripts/occ_session.py)
- [C:\AkkStack\code\utils\occ\scripts\start_occ.py](C:/AkkStack/code/utils/occ/scripts/start_occ.py)
- [C:\AkkStack\code\utils\occ\scripts\invoke_occ_tshark_session.ps1](C:/AkkStack/code/utils/occ/scripts/invoke_occ_tshark_session.ps1)
- [C:\AkkStack\code\utils\occ\scripts\monitor_occ_live.py](C:/AkkStack/code/utils/occ/scripts/monitor_occ_live.py)
- [C:\AkkStack\code\utils\occ\scripts\build_rof2_reference.py](C:/AkkStack/code/utils/occ/scripts/build_rof2_reference.py)
- [C:\AkkStack\code\utils\occ\codex-skill\README.md](C:/AkkStack/code/utils/occ/codex-skill/README.md)
- [C:\AkkStack\code\utils\occ\codex-skill\SKILL.md](C:/AkkStack/code/utils/occ/codex-skill/SKILL.md)
- [C:\AkkStack\code\utils\occ\start_occ.ps1](C:/AkkStack/code/utils/occ/start_occ.ps1)
- [C:\AkkStack\code\utils\occ\start_occ.sh](C:/AkkStack/code/utils/occ/start_occ.sh)

Capture session data lives under:

```text
C:\AkkStack\.codex\captures\sessions\
```

Typical session files:
- `session.json`
- `<session>.pcapng`
- `markers.jsonl`
- `detections.json`
- log files

## Troubleshooting

### Live feed is empty

Check:
- the capture session is actually running
- the interface is correct
- `Ethernet` is selected for your normal EQ client traffic
- the monitor view is not too restrictive
- `Count Limit`, `Unknown only`, or text filters are not hiding the rows

### Marker workflow feels noisy

Use smaller captures:
- one UI action at a time
- immediate marker after the action
- repeat the same action cleanly several times

### A packet family matches but the opcode is still unclear

That is normal.

Treat the packet family as valid evidence and use:
- packet family identity
- repeated triggers
- marker timing
- payload comparison
- ShowEQ/client/code validation

before promoting the opcode.

### Custom entry count does not move

Prefer creating the custom entry from the live feed so it captures the packet family key. That is more reliable than typing only a packet signature by hand.

## Summary

Use OCC like an investigation console:
- capture
- isolate
- mark
- group
- alias
- count
- alert
- hypothesize
- validate
- promote

The safest rule in this utility is simple:
- `Test Opcode` is for hypotheses
- `RoF2 Opcode` is for promoted working mappings
