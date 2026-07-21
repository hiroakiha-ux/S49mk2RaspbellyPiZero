// Shinonome 16-dot Japanese/ASCII text rendering demo.
//
// Renders the test phrase "test 調整中 こんにちは" at three sizes -- native
// 16x16 (scale=1), scale=2, and scale=4 -- one line per size, stacked on the
// left LCD. At scale=4 the line is wider than one screen (480px), so it
// spills onto the right LCD: each character is placed whole on whichever
// screen its position falls in (never cut in half), continuing on the right
// screen at the same y.
//
// This is a one-shot visual check, not a pass/fail test: run it and look at
// the screen(s). Only ever writes LCD packets, never touches HID or MIDI.
//
// Build (Linux/Pi only, needs libusb -- see CMakeLists.txt):
//   cmake -B build && cmake --build build --target lcd_shinonome_demo
//   ./build/lcd_shinonome_demo
#include <cstdio>
#include <string>

#include "display/lcd_canvas.h"
#include "display/lcd_packet.h"
#include "usb/bulk_display_device.h"

namespace {

// Decodes one UTF-8 codepoint starting at utf8[pos], advancing pos past it.
// Mirrors LcdCanvas's internal decoder (not exposed via the header) --
// duplicated here because this demo needs per-character width/position to
// decide which screen each character lands on, which DrawTextUtf8 alone
// doesn't expose.
char32_t DecodeUtf8At(const std::string& utf8, size_t& pos) {
  unsigned char b0 = static_cast<unsigned char>(utf8[pos]);
  if (b0 < 0x80) {
    ++pos;
    return b0;
  }
  int extra;
  char32_t cp;
  if ((b0 & 0xE0) == 0xC0) {
    extra = 1;
    cp = b0 & 0x1F;
  } else if ((b0 & 0xF0) == 0xE0) {
    extra = 2;
    cp = b0 & 0x0F;
  } else if ((b0 & 0xF8) == 0xF0) {
    extra = 3;
    cp = b0 & 0x07;
  } else {
    ++pos;
    return 0xFFFD;
  }
  if (pos + static_cast<size_t>(extra) >= utf8.size()) {
    ++pos;
    return 0xFFFD;
  }
  for (int i = 1; i <= extra; ++i) {
    unsigned char b =
        static_cast<unsigned char>(utf8[pos + static_cast<size_t>(i)]);
    if ((b & 0xC0) != 0x80) {
      ++pos;
      return 0xFFFD;
    }
    cp = (cp << 6) | (b & 0x3F);
  }
  pos += static_cast<size_t>(extra) + 1;
  return cp;
}

// Draws `text` at `scale` starting at y, splitting across `left`/`right`
// canvases at the x=kLcdWidth boundary so no character is cut in half.
// Returns the total pixel width the text would occupy (may exceed one
// screen's width).
int DrawSplitAcrossScreens(mk2::LcdCanvas& left, mk2::LcdCanvas& right, int y,
                            const std::string& text, int scale, uint8_t r,
                            uint8_t g, uint8_t b) {
  int cursor_x = 0;
  size_t pos = 0;
  while (pos < text.size()) {
    char32_t cp = DecodeUtf8At(text, pos);
    int char_width = (cp <= 0x7F) ? 8 * scale : 16 * scale;

    mk2::LcdCanvas& target =
        (cursor_x + char_width <= mk2::kLcdWidth) ? left : right;
    int target_x = (cursor_x + char_width <= mk2::kLcdWidth)
                        ? cursor_x
                        : cursor_x - mk2::kLcdWidth;

    if (cp <= 0x7F) {
      target.DrawCharShinonomeAscii(target_x, y, static_cast<char>(cp), scale,
                                     r, g, b);
    } else {
      target.DrawKanji16(target_x, y, cp, scale, r, g, b);
    }
    cursor_x += char_width;
  }
  return cursor_x;
}

}  // namespace

int main() {
  mk2::LcdBulkDevice lcd;
  if (!lcd.Open()) {
    std::fprintf(stderr, "lcd_shinonome_demo: LCD open failed: %s\n",
                 lcd.last_error().c_str());
    return 1;
  }

  const std::string text = u8"test 調整中 こんにちは";

  mk2::LcdCanvas left;
  mk2::LcdCanvas right;
  left.Clear(0, 0, 0);
  right.Clear(0, 0, 0);

  int y = 8;
  for (int scale : {1, 2, 4}) {
    int width = DrawSplitAcrossScreens(left, right, y, text, scale, 255, 255,
                                        255);
    std::fprintf(stderr,
                 "lcd_shinonome_demo: scale=%d width=%dpx (%s)\n", scale,
                 width,
                 width > mk2::kLcdWidth ? "overflows one screen -> right LCD"
                                         : "fits on one screen");
    y += 16 * scale + 8;
  }

  bool ok_left =
      lcd.WritePacket(mk2::BuildLcdPacket(mk2::kLcdScreenLeft, left));
  bool ok_right =
      lcd.WritePacket(mk2::BuildLcdPacket(mk2::kLcdScreenRight, right));
  if (!ok_left || !ok_right) {
    std::fprintf(stderr, "lcd_shinonome_demo: write failed: %s\n",
                 lcd.last_error().c_str());
    return 1;
  }

  std::fprintf(stderr, "lcd_shinonome_demo: wrote both screens\n");
  return 0;
}
