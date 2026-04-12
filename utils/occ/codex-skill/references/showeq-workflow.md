# ShowEQ Workflow

Use this reference when the request is specifically about captures, Wireshark or `tshark`, ShowEQ output, or narrowing down unknown packet behavior.

## Capture Strategy

- Reduce machine noise before starting a capture. Close unrelated network-heavy processes where practical.
- Prefer UDP-only capture filters for EverQuest traffic.
- Default to same-box capture. The EQEmu writeup notes that same-box capture is safe because packet logging is passive; use a proxy or middlebox only when you explicitly need packet modification or a separate vantage point.
- For Codex-driven work, prefer `tshark` as the main tool:
  - `-D` lists interfaces
  - `-i` selects the interface
  - `-f` applies a capture filter
  - `-a duration:NUM` or `-c NUM` stops automatically
  - `-w` writes a `.pcapng`
  - `-r` reopens a saved capture
  - `-Y` applies a display filter
  - `-T fields` plus repeated `-e` exports structured fields
  - `-x` or `--hexdump` emits packet bytes
- On this workstation, `C:\Program Files\Wireshark\tshark.exe` is installed and the loopback adapter is available as `\Device\NPF_Loopback`.
- For same-machine UI timing work, keep a background `tshark` session running while the user performs exactly one action and immediately notifies Codex. Mark that moment as an explicit timestamp so analysis can focus on the nearest traffic instead of the entire capture.
- A full login-through-enter-world capture can help identify the patch level and other contextual data, but it also captures account credentials.
- If a capture might be shared, remove packets containing account names, passwords, and character names before handing it to anyone else.
- Use broad login captures for context only. For actual packet analysis, switch to minimal action-specific captures as soon as you know what behavior you are studying.
- For actual packet analysis, capture a very small, focused interaction. Move the character to the place of interest, start the capture, perform the action, save the capture, and repeat if needed.
- Repeat the action or zone and retry when you suspect the client cached data and only sent it once.
- For remote live captures, prefer a capture filter narrowed to UDP traffic between your client host and the EverQuest server range or specific server IPs. For local EQEmu same-box work, loopback plus `udp` is usually the right starting point.
- Screenshots are useful alongside captures because visible values can help identify field placement and types.

## ShowEQ Patch Matching

- ShowEQ release tags correspond to client patch points.
- Use the ShowEQ tag date to line up the capture with the right patch era.
- In a ShowEQ release, the main opcode files of interest are `worldopcodes.xml` and `zoneopcodes.xml`.
- ShowEQ does not map every opcode for every patch. Use it as a strong hint source, not a complete ground truth database.

## Reading Captures

- Do not treat the raw packet capture as immediately readable application traffic.
- EQEmu documentation notes that captures are often compressed and carried as Combined or Oversized packets, so they must be unpacked before the application opcode is meaningful.
- Start with `tshark` summary or payload exports before opening Wireshark. This keeps the work scriptable and easier for Codex to repeat.
- Once the packets are unpacked, packet direction plus opcode bytes plus byte and string views are the fastest way to reason about likely structure.
- Focus on small samples. Large captures produce too many unmapped packets and make it harder to isolate the behavior you care about.

## When Mapping Fails

- Check the local EQEmu `utils/patches/patch_*.conf` mapping for the emulator-supported client first.
- For RoF2, also check the user-provided external sheet:
  - [64 Bit Opcode Table](https://docs.google.com/spreadsheets/d/1ZejpzNJBSVkSeisqiFd2ZyqvNl52XHe79ZlhnZ9UxBk/edit?gid=984146946#gid=984146946)
  - Use the RoF2 sheet's columns H and I as secondary reference data for known opcodes and notes.
- If ShowEQ has a partial mapping, use it to anchor the packet family or direction.
- If neither ShowEQ nor EQEmu identifies the opcode, the next step is client reverse engineering and pattern comparison in `eqgame.exe`.
