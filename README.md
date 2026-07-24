# S49 MK2 ⇄ SEQTRAK Raspberry Pi Zero 2 W bridge

Standalone C++ port of the `KompleteControl_MK2` Python protocol research
(`資料/KompleteControl_MK2-main_extracted/`), targeting a headless
Raspberry Pi Zero 2 W that USB-hosts a hub with:

- Native Instruments KOMPLETE KONTROL S49 MK2 (LCDs, Light Guide, button
  LEDs, knobs, jog wheel, keybed)
- YAMAHA SEQTRAK (11-track groovebox, MIDI in/out)

and relays/bridges between them, with an internal step sequencer.

## Layout

```
include/
  mk2_protocol.h       MK2 USB/HID protocol constants (VID/PID, report IDs,
                        LCD packet layout, button/knob/jog input maps, LED
                        report layouts, 0xA1-0xA4 assignment report layouts)
  seqtrak_protocol.h   SEQTRAK MIDI implementation constants (track/channel
                        map, CC map, SysEx envelope + top-level parameter
                        addresses)
  usb/                 HID (hidraw) + bulk LCD (libusb) device wrappers
  display/             RGB565 canvas + LCD packet builder
  midi/                ALSA rawmidi port + MK2<->SEQTRAK router
  seq/                 Minimal internal step sequencer
  app/                 Application wiring + entry point
  util/                Hex-dump helpers shared by --dry-run logging and tests/
src/                   Matching .cpp implementations
tests/                 Standalone bring-up/verification programs, see
                       tests/README.md and the phased testing guide below
```

Protocol details in the two top-level headers were extracted directly from
the Python research project's source and `protocol.md`, and from
`SEQTRAK_data_list_En_D0.pdf`'s "MIDI Data Format"/"MIDI Data Table"
sections. Comments in each header cite where a value came from and flag
anything still marked "inferred"/"unknown" upstream.

## Build (on the Raspberry Pi, or an aarch64/armhf cross environment)

```bash
sudo apt install build-essential cmake pkg-config libusb-1.0-0-dev libasound2-dev
cmake -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j$(nproc)
```

This was scaffolded and syntax-checked on macOS for the OS-portable pieces
(`mk2_protocol.h`, `seqtrak_protocol.h`, `display/*`, `seq/*`); the
`usb/*` (Linux `hidraw` + `libusb`) and `midi/*` (ALSA) sources are
Linux-only and need a Linux toolchain (the Pi itself, or an aarch64 Linux
cross-compiler) to build and link.

## Phase 0: hardware-free validation (do this first, on your Mac)

`tests/lcd_packet_selftest` needs no hardware and no Pi -- it builds with
any C++17 compiler and checks `BuildLcdPacket()`'s byte output against the
hex vectors the Python project's test suite confirmed against real S49 MK2
hardware. Run it before touching any device:

```bash
cmake -B build && cmake --build build --target lcd_packet_selftest
./build/lcd_packet_selftest   # expect: ALL TESTS PASSED
```

See `tests/README.md` for what else lives under `tests/` and when to run
each one.

## Runtime setup

- **USB HID permissions**: the MK2's HID interface shows up as
  `/dev/hidrawN`. Add a udev rule so it's readable/writable by a non-root
  user, e.g. `/etc/udev/rules.d/99-mk2.rules`:
  ```
  SUBSYSTEM=="hidraw", ATTRS{idVendor}=="17cc", MODE="0660", GROUP="plugdev"
  ```
- **USB bulk LCD access**: interface 3 (vendor class) has no kernel driver,
  so `libusb` can claim it directly as any user in `plugdev` with a
  matching udev rule, or run as root.
- **MIDI devices**: both the MK2 (class-compliant USB-MIDI over its audio
  interface) and SEQTRAK should enumerate as ALSA rawmidi devices under
  `/dev/snd/midiC*D*`. List them with `amidi -l` or `aconnect -l` to confirm
  the name substrings ("KOMPLETE KONTROL", "SEQTRAK") that
  `AlsaRawMidiPort::OpenByNameSubstring` matches against; adjust
  `src/app/controller_app.cpp` if your firmware reports different names.

## Run

```bash
./build/s49mk2_bridge            # live: writes to MK2 LCD + MIDI + SEQTRAK
./build/s49mk2_bridge --dry-run  # safe: reads hardware, never writes to it
./build/s49mk2_bridge --help
```

On startup it opens the MK2 HID + LCD bulk endpoints, opens both MIDI
ports, draws a static test frame to both LCDs, starts the MK2⇄SEQTRAK MIDI
relay, and starts the internal step sequencer (empty pattern, 120 BPM,
silent until steps are programmed). Ctrl-C stops cleanly.

### `--dry-run`

With `--dry-run`, every device is still opened and read from as normal, but
**nothing is ever written**: LCD packets are built (so you can confirm the
logic runs end to end) and then hex-dumped to stderr instead of sent over
the bulk endpoint; every MIDI message the relay or the control mapper would
have sent is decoded and printed instead of written to the MK2 or SEQTRAK
rawmidi port. This mirrors the original Python tool's
dry-run-before-`--execute` convention, and is the safe way to confirm the
whole pipeline (device discovery, HID decoding, packet/message
construction) before letting it actually touch either instrument.

### Recommended bring-up order

1. **`tests/lcd_packet_selftest`** on your Mac -- no hardware (see Phase 0
   above).
2. **Connect only the MK2** to the Pi via the hub. Confirm OS-level
   enumeration only: `lsusb` shows vendor `17cc`, `/dev/hidraw*` gains a
   node. This is pure kernel enumeration; nothing in this repo runs yet.
3. **`tests/hid_input_dump`** on the Pi -- reads MK2 knob/button/jog input
   and prints it, but never writes anything. Confirm turning a knob /
   pressing a button shows up correctly.
4. **`./build/s49mk2_bridge --dry-run`** with just the MK2 connected --
   confirms LCD packet construction and HID decoding end to end via the
   real app, with zero writes.
5. **Connect SEQTRAK** too, `amidi -l` to confirm its rawmidi name matches
   what `AlsaRawMidiPort::OpenByNameSubstring("SEQTRAK")` expects (adjust
   `src/app/controller_app.cpp` if not), then `--dry-run` again to see the
   MIDI relay's decoded traffic without sending anything.
6. **`./build/s49mk2_bridge`** (live) only once the above all look correct.
   Keep SEQTRAK's volume low / on headphones for the first run, in case a
   mis-scaled CC or an unexpected note happens.

## What's minimal / left as scaffolding

- **Step sequencer**: fixed 16-step, one-note-per-step patterns per track,
  driven by a simple clock thread. No swing, per-step probability,
  automation, or persistence yet; `StepSequencer::SetStep` is the
  programming entry point for a future MK2-pad-driven step editor.
- **MK2 -> SEQTRAK control mapping**: knobs and Function buttons are
  translated to the SEQTRAK's documented default CC assignment
  (`kDefaultKnobCcBase`/`kDefaultFunctionButtonCcBase` in
  `mk2_protocol.h`, channel 1). On real hardware, knob rotation is received
  through USB-MIDI Control Change messages, not through the HID knob-value
  fields: Knob 1 is CC `0x0E`, Knob 2 is CC `0x0F`, and the third byte is
  the absolute value `0..127`. HID byte 7 is used only for knob-touch
  gestures. It does not yet program the MK2's own
  `0xA1` HID assignment report, drive Light Guide/button LED feedback from
  SEQTRAK state, or expose the jog wheel/pedals/touch-strip.
- **LCD UI**: only a static two-color test frame; no text rendering, bar
  graphs, or live SEQTRAK status display yet (see `display/lcd_canvas.h`
  for the primitives available to build one).
- **SysEx**: `seqtrak_protocol.h` documents the envelope and top-level
  block addresses (tempo, volume, track general, sound common, etc.); the
  router currently relays SysEx transparently but doesn't construct
  Parameter Change / Dump Request messages itself. Add builders alongside
  `mk2::BuildControlChange` in `midi/router.h` as needed.
