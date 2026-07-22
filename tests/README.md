# tests/

Small, standalone programs for the incremental/safe hardware bring-up
described in the top-level `README.md`. Neither of these is a unit-test
framework -- they're minimal, dependency-free programs you run by hand and
read the output of, matching the original Python project's philosophy of
"preview before you touch hardware."

| Program | What it needs | What it does | Risk |
|---|---|---|---|
| `lcd_packet_selftest` | Nothing (builds on macOS or Linux) | Compares `BuildLcdPacket()`'s byte output against hex vectors from the Python project's `tests/test_display_packet.py`, which were themselves confirmed against real S49 MK2 hardware. | None -- no hardware, no I/O. |
| `seqtrak_sysex_selftest` | Nothing (builds on macOS or Linux) | Compares the SEQTRAK SysEx builders/parser (`midi/seqtrak_sysex.h`) against hand-computed byte vectors from `資料/SEQTRAK_data_list_En_D0.pdf`. | None -- no hardware, no I/O. |
| `hid_input_dump` | A Pi + MK2 connected via hub | Opens the MK2's `hidraw` device and prints every changed input report (Function buttons, panel buttons, knobs, jog wheel), decoded and as a hex dump. | None -- calls `HidDevice::ReadReport()` only, never `WriteReport()`. |
| `seqtrak_param_probe` | A Pi + SEQTRAK connected, ALSA MIDI port visible | `--set VALUE`: sends a Parameter Change (write) -- **confirmed working for every block tried**. Without `--set`: sends a Parameter Request and waits for a reply (default 8s timeout, `--timeout-ms` to adjust) -- **confirmed working**, replies just take longer than 1.5s. Use `--track`/`--field` for the four wired-up Project Track General fields, or `--address HH MM LL` for any other block. | `--set` writes the given value to real hardware immediately (e.g. changes track volume audibly) -- not a read-only tool. |
| `seqtrak_block_scan` | A Pi + SEQTRAK connected, ALSA MIDI port visible | Sends a Parameter Request for one representative address in every top-level parameter block (System, Project Common/Track, Sound Common, Element, DX, SAMPLER) and reports reply/timeout + latency for each, with a generous per-block timeout (default 10s). | None -- only sends Parameter Request (read), never writes. |
| `seqtrak_name_probe` | A Pi + SEQTRAK connected, ALSA MIDI port visible | Selects a factory preset via Bank Select + Program Change, then reads back Sound Common Name bytes to test whether the device actually populates that block on preset recall (vs. names only existing in companion editor apps). | Sends Bank Select/Program Change (changes the track's sound) -- not read-only. |
| `lcd_text_demo` | A Pi + MK2 connected | Draws sample text at three sizes (scale 1/3/6, using the built-in `font8x8_basic` bitmap font) to the left LCD, to visually confirm `LcdCanvas::DrawText` renders correctly on real hardware. Not pass/fail -- look at the screen. | Writes one LCD packet; no HID/MIDI involved. |
| `lcd_shinonome_demo` | A Pi + MK2 connected | Draws "test 調整中 こんにちは" (Shinonome 16-dot Gothic font, half-width ASCII + full-width kanji/kana) at scale 1/2/4, one line per size; if a line is wider than one screen it continues on the right LCD. | Writes both LCD packets; no HID/MIDI involved. |

## Recommended order

1. **`lcd_packet_selftest` and `seqtrak_sysex_selftest`, on your Mac, before anything else.**
   ```bash
   cmake -B build && cmake --build build --target lcd_packet_selftest seqtrak_sysex_selftest
   ./build/lcd_packet_selftest
   ./build/seqtrak_sysex_selftest
   ```
   Expect `ALL TESTS PASSED` from both. These validate the LCD packet and
   SEQTRAK SysEx builders are byte-correct against hardware-verified/PDF
   -verified vectors, with zero risk and zero hardware required.

2. **`hid_input_dump`, on the Pi, once the MK2 is connected and enumerates**
   (see the "Phase 2" USB enumeration check in the top-level README).
   ```bash
   cmake -B build && cmake --build build --target hid_input_dump
   ./build/hid_input_dump
   ```
   Turn a knob, press a Function button, touch the jog wheel, and confirm
   the decoded output matches what you did. This is purely observational;
   it cannot write anything to the device, so there is nothing to undo if
   something looks wrong -- just Ctrl-C and re-run.

3. **`seqtrak_param_probe`/`seqtrak_block_scan`, on the Pi, once SEQTRAK's
   MIDI port is visible.**
   ```bash
   cmake -B build && cmake --build build --target seqtrak_param_probe seqtrak_block_scan
   ./build/seqtrak_block_scan
   ./build/seqtrak_param_probe --track 8 --field volume --set 0
   ```
   **Settled (2026-07-21):** both Parameter Change (write) and Parameter
   Request (read) are confirmed working for nearly every block -- a full
   `seqtrak_block_scan` run got replies from 28/31 blocks (System Common,
   Project Common/Track, Sound Common, Element, DX, SAMPLER), almost all
   within milliseconds. Only System General and the two Bulk Header/Footer
   framing addresses didn't reply within 10s. An earlier "some blocks never
   reply" conclusion was a false negative from too short a timeout (1.5s) --
   always allow several seconds when testing SEQTRAK SysEx replies.

4. **`lcd_text_demo`, on the Pi, once the MK2's LCD is reachable** (see the
   "Phase" LCD bulk-endpoint check in the top-level README).
   ```bash
   cmake -B build && cmake --build build --target lcd_text_demo
   ./build/lcd_text_demo
   ```
   Draws "SMALL Hello 123" / "MEDIUM 123" / "LARGE 8" at increasing scale on
   the left screen -- confirms the bitmap font renders correctly before
   building any real UI on top of it.

5. **`lcd_shinonome_demo`, on the Pi, to confirm Japanese text rendering.**
   ```bash
   cmake -B build && cmake --build build --target lcd_shinonome_demo
   ./build/lcd_shinonome_demo
   ```
   Draws "test 調整中 こんにちは" at scale 1 (native 16x16), 2, and 4. At
   scale 1 (176px) and 2 (352px) it fits on the left screen; at scale 4
   (704px) it overflows 480px and continues on the right screen. Watch
   stderr for the measured width per scale.

6. Only after all of the above look correct, move on to
   `./build/s49mk2_bridge --dry-run` (see the top-level README's phased
   guide) before ever running the bridge live.
