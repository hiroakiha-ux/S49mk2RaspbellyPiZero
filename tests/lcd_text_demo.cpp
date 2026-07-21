// LCD text rendering demo: draws English text + digits at three sizes
// (small/medium/large) on the MK2's left screen, using the built-in 8x8
// bitmap font (display/font8x8_basic.h, LcdCanvas::DrawText).
//
// This is a one-shot visual check, not a pass/fail test: run it and look at
// the screen. Only ever writes an LCD packet (bulk OUT), never touches HID
// or MIDI, so it's safe to run repeatedly.
//
// Build (Linux/Pi only, needs libusb -- see CMakeLists.txt):
//   cmake -B build && cmake --build build --target lcd_text_demo
//   ./build/lcd_text_demo
#include <cstdio>

#include "display/lcd_canvas.h"
#include "display/lcd_packet.h"
#include "usb/bulk_display_device.h"

int main() {
  mk2::LcdBulkDevice lcd;
  if (!lcd.Open()) {
    std::fprintf(stderr, "lcd_text_demo: LCD open failed: %s\n",
                 lcd.last_error().c_str());
    return 1;
  }

  mk2::LcdCanvas canvas;
  canvas.Clear(0, 0, 0);

  // scale 1: 8px tall glyphs ("small")
  canvas.DrawText(16, 24, "SMALL Hello 123", 1, 0, 255, 0);
  // scale 3: 24px tall glyphs ("medium")
  canvas.DrawText(16, 60, "MEDIUM 123", 3, 0, 200, 255);
  // scale 6: 48px tall glyphs ("large")
  canvas.DrawText(16, 140, "LARGE 8", 6, 255, 128, 0);

  bool ok = lcd.WritePacket(mk2::BuildLcdPacket(mk2::kLcdScreenLeft, canvas));
  if (!ok) {
    std::fprintf(stderr, "lcd_text_demo: write failed: %s\n",
                 lcd.last_error().c_str());
    return 1;
  }

  std::fprintf(stderr,
               "lcd_text_demo: wrote small/medium/large text to the left "
               "screen\n");
  return 0;
}
