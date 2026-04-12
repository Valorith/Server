---
name: Eqemu Opcode Command Center
description: Launch the Opcode Command Center first by starting the OCC server if needed and opening the OCC page in the browser, then resolve EverQuest and EQEmu packet opcodes from `tshark` captures and exports, ShowEQ references, raw opcode values, or client patch files, and trace them into EQEmu handlers and packet structs. Use when Codex needs to launch OCC in the browser, capture EverQuest traffic from the CLI, inspect `.pcapng` files, export packet fields or payload hex with `tshark`, map `xx,yy` opcode bytes or `0x####` values to `OP_*` names for Titanium, SoF, SoD, UF, RoF, or RoF2, compare results with `utils/patches/patch_*.conf`, or follow an opcode into `common`, `world`, `zone`, `loginserver`, `ucs`, or related code.
---

# Eqemu Opcode Command Center

This is a repo-local snapshot of the OCC Codex skill stored under `C:\AkkStack\code\utils\occ\codex-skill` as an optional tool for Codex users.

Use this skill for opcode work that starts in the Opcode Command Center and may continue through `tshark` capture or export, ShowEQ output, a focused packet capture, or a raw client opcode before ending in the local EQEmu codebase. Launch OCC first unless the user explicitly says not to. Use `C:\AkkStack\code\utils\occ\start_occ.ps1`, which starts the OCC server when it is not already running and then opens the OCC page in the browser. Treat `tshark` as the primary working surface for Codex after OCC is open. Use Wireshark only when a GUI view or manual drill-down is clearly more efficient.

## First Step

Always launch the Opcode Command Center first with `C:\AkkStack\code\utils\occ\start_occ.ps1`. That launcher ensures the OCC server is running on the local machine and then opens the OCC page in the browser so the dashboard, local bridge, and live session state are ready before any opcode capture or analysis work begins.

## Live Timing Workflow

Use this flow when the user wants Codex to watch the same machine while they click a client UI element.

1. Launch the OCC dashboard first with `C:\AkkStack\code\utils\occ\start_occ.ps1`. Do not manually reimplement this startup flow unless the launcher script is broken, because it already handles the "start server if needed, then open browser" behavior.
2. Treat OCC as the primary workflow surface for the live session:
   - opcode registry and notes stay in OCC
   - current `tshark` session state stays in OCC
   - OCC can start, stop, and mark local capture sessions directly through its local bridge server
   - user action markers/bookmarks appear in OCC after each `Mark`
3. Start a named capture session with `scripts/invoke_eqemu_tshark_session.ps1 -Action Start`.
4. Tell the user to perform one UI action at a time and then immediately send a short message such as `mark now: inventory button`.
5. As soon as the user sends that message, run `-Action Mark` with the label they provided.
6. After the user finishes the small test sequence, stop the session with `-Action Stop`.
7. Run `scripts/analyze_eqemu_marker.py` against the latest marker to rank the packets nearest the click time on both sides of the marker and surface likely opcode candidates.

Keep the test tight:
- one UI action per marker
- at least one repeated run for the same action
- relog or zone if the client may have cached the result
- avoid parallel client actions during the window you care about
- after each Start, Mark, Stop, or Status action, the session script republishes live state to `C:\AkkStack\code\utils\occ\data\live-session.json` so OCC stays current

## Workflow

1. Determine the target client and capture source.
   - Prefer `tshark` for capture, filtering, export, and repeatable analysis that Codex can run directly.
   - Use Wireshark only as a secondary viewer for ad hoc packet inspection.
   - Use ShowEQ as a reference source for patch-era opcode work and targeted packet investigation.
   - Read `references/showeq-workflow.md` if you need the documented capture strategy, ShowEQ patch selection flow, `tshark` capture notes, or limitations around compressed and combined packets.
   - Split capture work into two modes:
     - context capture: login through enter-world when you need patch-era clues, guild list data, character select structures, or other broad context
     - focused capture: a tiny action-specific sample when you are identifying a specific interaction or packet family
    - Match the target client to the EQEmu patch file:
      - `Titanium` -> `utils/patches/patch_Titanium.conf`
      - `SoF` -> `utils/patches/patch_SoF.conf`
      - `SoD` -> `utils/patches/patch_SoD.conf`
      - `UF` -> `utils/patches/patch_UF.conf`
     - `RoF` -> `utils/patches/patch_RoF.conf`
     - `RoF2` -> `utils/patches/patch_RoF2.conf`

2. Resolve the opcode bytes first.
   - Use `scripts/eqemu_opcode_lookup.py` for single lookups and line-by-line annotation.
   - Use `scripts/invoke_eqemu_tshark_capture.ps1` to create focused `.pcapng` captures from the CLI when Codex should perform the capture itself.
   - Use `scripts/export_eqemu_tshark_fields.ps1` to reopen a capture, apply a display filter, and export packet metadata plus payload bytes without opening Wireshark.
   - ShowEQ-style bytes such as `bc,33` are little-endian application opcode bytes and correspond to `0x33bc`.
   - EQEmu serializes application opcodes as little-endian values in `common/eq_packet.cpp`.
   - If the low byte is `00`, EQEmu can encode the application opcode with a leading `00` byte before the 2-byte opcode. Account for that when reading raw bytes.

3. Trace the resolved `OP_*` name through the emulator.
   - Patch loader and name mapping logic live in `common/opcodemgr.cpp`.
   - Generic packet structs live in `common/eq_packet_structs.h`.
   - Client-specific structs and encode/decode logic live in `common/patches/*_structs.h`.
   - Reliable stream framing lives in `common/net/reliable_stream_structs.h`.
   - For RoF2 specifically, use the external 64-bit opcode spreadsheet as a secondary clue source when local mappings are incomplete or when you need corroboration for a candidate opcode. The user-provided reference is:
     - [64 Bit Opcode Table](https://docs.google.com/spreadsheets/d/1ZejpzNJBSVkSeisqiFd2ZyqvNl52XHe79ZlhnZ9UxBk/edit?gid=984146946#gid=984146946)
     - Treat columns H and I on the RoF2 sheet as user-provided known-opcode reference data, not as a replacement for local `utils/patches/patch_RoF2.conf`.
   - Search for an opcode name with PowerShell:

```powershell
Get-ChildItem C:\AkkStack\code\common, C:\AkkStack\code\world, C:\AkkStack\code\zone, C:\AkkStack\code\loginserver, C:\AkkStack\code\ucs -Recurse -File |
  Select-String -Pattern 'OP_ChannelMessage' |
  Select-Object -First 20 Path, LineNumber, Line
```

4. Treat captures as staged evidence, not direct truth.
   - Raw captures are often compressed, combined, or oversized before you can reason about application packets.
   - Keep captures focused on a single action whenever possible.
   - Repeat the same action at least once more so you can separate signal from zone noise.
   - If the client may have cached the data, zone or relog and repeat the capture.
   - When sharing a broad login capture, remove packets that expose account credentials and character names first.
   - If structure is still unclear after tracing the opcode, compare repeated captures, isolate constants versus server-specific values, and only then move into client reversing or packet simulation.

## Commands

Launch OCC before a live timing session:

```powershell
powershell -ExecutionPolicy Bypass -File "C:\AkkStack\code\utils\occ\start_occ.ps1"
```

Single lookup from ShowEQ bytes:

```powershell
python C:\AkkStack\code\utils\occ\codex-skill\scripts\eqemu_opcode_lookup.py lookup --client Titanium --raw-bytes "13,72" --with-code-refs
```

Single lookup from a raw opcode:

```powershell
python C:\AkkStack\code\utils\occ\codex-skill\scripts\eqemu_opcode_lookup.py lookup --client RoF2 --opcode 0x7a09
```

Reverse lookup from an emulator opcode name:

```powershell
python C:\AkkStack\code\utils\occ\codex-skill\scripts\eqemu_opcode_lookup.py lookup --client UF --name OP_ZoneEntry
```

Annotate a ShowEQ text export line-by-line:

```powershell
python C:\AkkStack\code\utils\occ\codex-skill\scripts\eqemu_opcode_lookup.py annotate --client Titanium --input C:\captures\showeq.txt
```

List capture interfaces through `tshark`:

```powershell
powershell -ExecutionPolicy Bypass -File "C:\AkkStack\code\utils\occ\codex-skill\scripts\invoke_eqemu_tshark_capture.ps1" -ListInterfaces
```

Take a focused 20-second UDP loopback capture for local EQEmu work:

```powershell
powershell -ExecutionPolicy Bypass -File "C:\AkkStack\code\utils\occ\codex-skill\scripts\invoke_eqemu_tshark_capture.ps1" -DurationSec 20 -CaptureFilter "udp" -OpenInWireshark
```

Take a broader context capture for a remote live server by narrowing traffic to a server IP:

```powershell
powershell -ExecutionPolicy Bypass -File "C:\AkkStack\code\utils\occ\codex-skill\scripts\invoke_eqemu_tshark_capture.ps1" -Interface "Ethernet" -DurationSec 90 -CaptureFilter "udp and host 69.174.0.0/16"
```

Export packet fields and UDP payload hex from a saved capture:

```powershell
powershell -ExecutionPolicy Bypass -File "C:\AkkStack\code\utils\occ\codex-skill\scripts\export_eqemu_tshark_fields.ps1" -InputPath "C:\AkkStack\.codex\captures\eqemu-20260411-130000.pcapng" -DisplayFilter "udp" -Mode payloads
```

Save the same export to a TSV file for later annotation:

```powershell
powershell -ExecutionPolicy Bypass -File "C:\AkkStack\code\utils\occ\codex-skill\scripts\export_eqemu_tshark_fields.ps1" -InputPath "C:\AkkStack\.codex\captures\eqemu-20260411-130000.pcapng" -DisplayFilter "udp" -Mode payloads -OutputPath "C:\AkkStack\.codex\captures\eqemu-payloads.tsv"
```

Start a live timing session:

```powershell
powershell -ExecutionPolicy Bypass -File "C:\AkkStack\code\utils\occ\codex-skill\scripts\invoke_eqemu_tshark_session.ps1" -Action Start -SessionName "inventory-test" -Interface loopback -CaptureFilter "udp"
```

Mark the click time as soon as the user says they clicked:

```powershell
powershell -ExecutionPolicy Bypass -File "C:\AkkStack\code\utils\occ\codex-skill\scripts\invoke_eqemu_tshark_session.ps1" -Action Mark -SessionName "inventory-test" -Label "inventory-button"
```

Stop the session and analyze the latest marker:

```powershell
powershell -ExecutionPolicy Bypass -File "C:\AkkStack\code\utils\occ\codex-skill\scripts\invoke_eqemu_tshark_session.ps1" -Action Stop -SessionName "inventory-test"
python C:\AkkStack\code\utils\occ\codex-skill\scripts\analyze_eqemu_marker.py --session-name inventory-test --client Titanium --marker latest
```

## Rules

- Prefer `tshark` for capture, reread, filtering, and export. Use Wireshark only when you need a GUI packet tree or packet-by-packet visual inspection.
- Default local EQEmu CLI captures to the loopback adapter unless the user says the client and server are on different machines.
- Use same-box capture by default. Only switch to a proxy or separate capture host when the user explicitly wants that setup.
- Start with a broad login capture only when patch identification or high-level context matters. Otherwise skip straight to a minimal action capture.
- For timing-sensitive UI work, always use a named session plus explicit markers. Do not rely on memory or rough chat timing alone.
- Do not assume a packet capture can be read directly without unpacking compression, combined packets, or oversized packets.
- Use the local patch files as the authoritative mapping for emulator-supported clients.
- For RoF2, cross-check unresolved candidates against the user-provided 64-bit opcode spreadsheet before concluding that a packet is still unidentified.
- If ShowEQ has a name but EQEmu does not map that opcode for the target client, report the mismatch explicitly instead of guessing.
- If the opcode remains unknown after checking ShowEQ and EQEmu patch files, surface client reversing as the next step rather than inventing a name.
