// Simple RGB565 framebuffer for one MK2 LCD (480x272), with a handful of
// primitive drawing operations. Higher-level UI drawing (text layout, bar
// graphs, etc.) belongs in the app layer; this is intentionally minimal.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "mk2_protocol.h"

namespace mk2 {

class LcdCanvas {
 public:
  LcdCanvas(int width = kLcdWidth, int height = kLcdHeight);

  int width() const { return width_; }
  int height() const { return height_; }

  void Clear(uint8_t r, uint8_t g, uint8_t b);
  void SetPixel(int x, int y, uint8_t r, uint8_t g, uint8_t b);
  void FillRect(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b);
  void DrawRect(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b);

  // Draws one glyph from the built-in 8x8 basic-Latin font (font8x8_basic.h),
  // top-left at (x, y), each font pixel drawn as a `scale`x`scale` block.
  // Characters outside the basic Latin range (0-127) are skipped.
  void DrawChar(int x, int y, char c, int scale, uint8_t r, uint8_t g,
                uint8_t b);

  // Draws `text` left-to-right starting at (x, y), one DrawChar per
  // character with no extra inter-character gap (the font's own blank
  // right-hand column provides spacing). Returns the pixel width drawn,
  // i.e. `8 * scale * text.size()`.
  int DrawText(int x, int y, const std::string& text, int scale, uint8_t r,
               uint8_t g, uint8_t b);

  // Pixel width/height DrawText would occupy, without drawing anything.
  static int TextWidth(size_t char_count, int scale) {
    return 8 * scale * static_cast<int>(char_count);
  }
  static int TextHeight(int scale) { return 8 * scale; }

  // Draws one glyph from the Shinonome 16-dot Gothic half-width ASCII table
  // (font_shinonome16.h), 8 wide x 16 tall -- half the width of DrawKanji16
  // at the same scale, matching standard Japanese half-width/full-width
  // text layout. `c` outside printable ASCII (0x20-0x7E) is skipped.
  void DrawCharShinonomeAscii(int x, int y, char c, int scale, uint8_t r,
                                uint8_t g, uint8_t b);

  // Draws one glyph from the Shinonome 16-dot Gothic kanji/kana table
  // (font_shinonome16.h, currently a small hand-picked set, not the full
  // JIS X 0208 range), 16 wide x 16 tall. If `codepoint` isn't in the table,
  // draws a hollow placeholder box instead so missing glyphs are obvious
  // during testing rather than silently vanishing.
  void DrawKanji16(int x, int y, char32_t codepoint, int scale, uint8_t r,
                    uint8_t g, uint8_t b);

  // Draws UTF-8 text left-to-right starting at (x, y), dispatching each
  // decoded codepoint to DrawCharShinonomeAscii (half-width, 8*scale) or
  // DrawKanji16 (full-width, 16*scale). Returns the pixel width drawn.
  int DrawTextUtf8(int x, int y, const std::string& utf8_text, int scale,
                    uint8_t r, uint8_t g, uint8_t b);

  // Pixel width DrawTextUtf8 would occupy, without drawing anything.
  static int MeasureUtf8Width(const std::string& utf8_text, int scale);

  // Raw RGB565 big-endian pixel buffer, row-major, ready to embed in an LCD
  // packet (see display/lcd_packet.h).
  const std::vector<uint8_t>& pixels() const { return pixels_; }

 private:
  int width_;
  int height_;
  std::vector<uint8_t> pixels_;  // 2 bytes per pixel, big-endian RGB565
};

}  // namespace mk2
