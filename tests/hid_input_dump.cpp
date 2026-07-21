// Read-only MK2 HID input dump tool.
//
// Opens the MK2's hidraw interface and prints every changed input report,
// decoded (Function buttons, known panel buttons, knobs, jog wheel) plus a
// raw hex dump. This program calls HidDevice::ReadReport() only -- it never
// calls WriteReport(), so it cannot alter LEDs, the Light Guide, or any
// control/keyzone assignment on the device. Safe to run repeatedly against
// real hardware.
//
// This is "Phase 3" of the testing plan in ../README.md: run this after the
// MK2 shows up in `lsusb`/`/dev/hidraw*` but before running the full bridge
// (which also opens the LCD bulk endpoint and both MIDI ports).
//
// --wake-lcd: on real hardware, the eight LCD-adjacent Function buttons have
// been observed to report nothing at all over HID (byte 1 never changes)
// until a frame has been written to the LCD bulk endpoint at least once --
// consistent with them being soft keys whose input is only live once a host
// has claimed the display. Pass --wake-lcd to write one small solid-color
// test rectangle to each screen (the same kind of small, low-risk write the
// original Python project used as its "safer first LCD write test") before
// entering the read loop, and see whether Function-button presses start
// showing up.
//
// Build (on the Pi -- this is Linux-only, see CMakeLists.txt):
//   cmake -B build && cmake --build build --target hid_input_dump
//   ./build/hid_input_dump [--wake-lcd]
#include <cstdio>
#include <cstring>
#include <csignal>
#include <vector>

#include "display/lcd_canvas.h"
#include "display/lcd_packet.h"
#include "mk2_protocol.h"
#include "usb/bulk_display_device.h"
#include "usb/hid_device.h"
#include "util/hex_dump.h"

namespace {

volatile sig_atomic_t g_stop = 0;

void HandleSignal(int) { g_stop = 1; }

bool EventActive(const std::vector<uint8_t>& report,
                  const mk2::ButtonInputEvent& event) {
  if (static_cast<size_t>(event.byte_index) >= report.size()) return false;
  uint8_t value = report[event.byte_index];
  if (event.match == mk2::ButtonMatch::kMask) {
    return (value & event.value) == event.value;
  }
  return value == event.value;
}

const char* JogControlName(uint8_t value) {
  switch (value) {
    case mk2::kJogIdle:
      return "idle";
    case mk2::kJogTouch:
      return "touch";
    case mk2::kJogPress:
      return "press";
    case mk2::kJogLeft:
      return "left-click";
    case mk2::kJogUp:
      return "up-click";
    case mk2::kJogDown:
      return "down-click";
    case mk2::kJogRight:
      return "right-click";
    default:
      return "unknown";
  }
}

int JogTurnDelta(uint8_t previous_value, uint8_t current_value) {
  int delta = static_cast<int>(current_value) - static_cast<int>(previous_value);
  if (delta == 15) return -1;
  if (delta == -15) return 1;
  if (delta > 8) return delta - 16;
  if (delta < -8) return delta + 16;
  return delta;
}

void DecodeAndPrint(const std::vector<uint8_t>& previous,
                     const std::vector<uint8_t>& current) {
  if (current.empty() || current[0] != mk2::kHidReportInput) return;

  // TEMPORARY (A/B regression check): ground-truth dump of the raw byte
  // that Function1..8 are supposed to live in, with no decode table
  // involved at all. If this line never appears while pressing the
  // LCD-adjacent Function buttons, the byte itself isn't changing -- the
  // buttons aren't reporting, independent of any decode logic below. If it
  // *does* change here but nothing prints from the two loops below, that
  // points to a real bug in the decode tables.
  if (current.size() > 1 &&
      (previous.size() <= 1 || current[1] != previous[1])) {
    std::printf("  [raw check] byte[1]: 0x%02x -> 0x%02x\n",
                previous.size() > 1 ? previous[1] : 0, current[1]);
  }

  // TEMPORARY (A/B regression check): the original Function-button-only
  // loop that was removed in the last change, reinstated here so it runs
  // side by side with the table below on identical input.
  for (size_t i = 0; i < mk2::kFunctionButtonMasks.size(); ++i) {
    uint8_t mask = mk2::kFunctionButtonMasks[i];
    bool was = static_cast<size_t>(mk2::kInputByteFunctionButtons) <
                   previous.size() &&
               (previous[mk2::kInputByteFunctionButtons] & mask);
    bool now = static_cast<size_t>(mk2::kInputByteFunctionButtons) <
                   current.size() &&
               (current[mk2::kInputByteFunctionButtons] & mask);
    if (now && !was) std::printf("  [old loop] Function%zu: pressed\n", i + 1);
    if (!now && was) std::printf("  [old loop] Function%zu: released\n", i + 1);
  }

  // Function1..8 are also covered by kKnownButtonInputEvents (byte 1, same
  // masks, plus LED ids) -- this is the current unified table, printing
  // both directions, so each transition is reported exactly once outside of
  // this A/B test.
  for (size_t i = 0; i < mk2::kKnownButtonInputEventCount; ++i) {
    const mk2::ButtonInputEvent& event = mk2::kKnownButtonInputEvents[i];
    bool was = !previous.empty() && EventActive(previous, event);
    bool now = EventActive(current, event);
    if (now && !was) {
      std::printf("  [new loop] %s: pressed (led id %d)\n", event.name,
                  event.led_id);
    } else if (!now && was) {
      std::printf("  [new loop] %s: released (led id %d)\n", event.name,
                  event.led_id);
    }
  }

  if (current.size() >
          static_cast<size_t>(mk2::kInputKnobValueBase + 2 * mk2::kKnobCount - 1) &&
      previous.size() == current.size()) {
    for (int knob = 0; knob < mk2::kKnobCount; ++knob) {
      int offset = mk2::kInputKnobValueBase + knob * 2;
      uint16_t prev_value = static_cast<uint16_t>(
          previous[offset] | (previous[offset + 1] << 8));
      uint16_t cur_value = static_cast<uint16_t>(
          current[offset] | (current[offset + 1] << 8));
      if (prev_value != cur_value) {
        std::printf("  Knob%d: %u -> %u\n", knob + 1, prev_value, cur_value);
      }
    }
  }

  if (static_cast<size_t>(mk2::kInputByteJogControl) < current.size()) {
    uint8_t cur_jog = current[mk2::kInputByteJogControl];
    uint8_t prev_jog = static_cast<size_t>(mk2::kInputByteJogControl) <
                                previous.size()
                            ? previous[mk2::kInputByteJogControl]
                            : 0;
    if (cur_jog != prev_jog) {
      std::printf("  Jog control: %s (0x%02x)\n", JogControlName(cur_jog),
                  cur_jog);
    }
  }

  if (static_cast<size_t>(mk2::kInputByteJogTurn) < current.size() &&
      static_cast<size_t>(mk2::kInputByteJogTurn) < previous.size()) {
    uint8_t prev_turn = previous[mk2::kInputByteJogTurn];
    uint8_t cur_turn = current[mk2::kInputByteJogTurn];
    if (prev_turn != cur_turn) {
      std::printf("  Jog turn: delta=%+d (raw 0x%x -> 0x%x)\n",
                  JogTurnDelta(prev_turn, cur_turn), prev_turn, cur_turn);
    }
  }
}

void WakeLcd() {
  mk2::LcdBulkDevice lcd;
  if (!lcd.Open()) {
    std::fprintf(stderr,
                 "hid_input_dump: --wake-lcd: LCD open failed (%s) -- "
                 "continuing without it\n",
                 lcd.last_error().c_str());
    return;
  }

  constexpr int kSize = 32;
  mk2::LcdCanvas left(kSize, kSize);
  left.Clear(0, 60, 0);  // dim green
  lcd.WritePacket(mk2::BuildLcdPacket(mk2::kLcdScreenLeft, 0, 0, kSize, kSize,
                                       left.pixels()));

  mk2::LcdCanvas right(kSize, kSize);
  right.Clear(0, 0, 60);  // dim blue
  lcd.WritePacket(mk2::BuildLcdPacket(mk2::kLcdScreenRight, 0, 0, kSize, kSize,
                                       right.pixels()));

  std::fprintf(stderr,
               "hid_input_dump: --wake-lcd: wrote a %dx%d test rectangle to "
               "both LCDs\n",
               kSize, kSize);
}

}  // namespace

int main(int argc, char** argv) {
  bool wake_lcd = false;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--wake-lcd") == 0) wake_lcd = true;
  }

  std::signal(SIGINT, HandleSignal);
  std::signal(SIGTERM, HandleSignal);

  if (wake_lcd) WakeLcd();

  mk2::HidDevice hid;
  if (!hid.OpenFirstSupported()) {
    std::fprintf(stderr, "hid_input_dump: %s\n", hid.last_error().c_str());
    std::fprintf(stderr,
                 "hid_input_dump: is the MK2 connected? check `lsusb` and "
                 "/dev/hidraw* permissions (see README.md)\n");
    return 1;
  }
  std::fprintf(stderr,
               "hid_input_dump: opened %s (read-only -- this program never "
               "writes to the device)\n",
               hid.path().c_str());
  std::fprintf(stderr,
               "hid_input_dump: turn knobs / press buttons / move the jog "
               "wheel. Ctrl-C to stop.\n\n");

  std::vector<uint8_t> previous;
  while (!g_stop) {
    auto report = hid.ReadReport(/*timeout_ms=*/200);
    if (!report.has_value()) continue;
    const std::vector<uint8_t>& current = *report;
    if (current == previous) continue;

    std::printf("---- report (%zu bytes) ----\n%s", current.size(),
                mk2util::HexDump(current).c_str());
    DecodeAndPrint(previous, current);

    previous = current;
  }

  std::fprintf(stderr, "\nhid_input_dump: stopped\n");
  return 0;
}
