#include "display/lcd_canvas.h"

#include <algorithm>

#include "display/font8x8_basic.h"
#include "display/font_shinonome16.h"

namespace mk2 {

namespace {

// Decodes one UTF-8 codepoint starting at utf8[pos], advancing pos past it.
// Malformed/truncated sequences decode as U+FFFD and advance by 1 byte, so a
// bad byte can't stall the caller in an infinite loop.
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
    unsigned char b = static_cast<unsigned char>(utf8[pos + static_cast<size_t>(i)]);
    if ((b & 0xC0) != 0x80) {
      ++pos;
      return 0xFFFD;
    }
    cp = (cp << 6) | (b & 0x3F);
  }
  pos += static_cast<size_t>(extra) + 1;
  return cp;
}

}  // namespace

LcdCanvas::LcdCanvas(int width, int height)
    : width_(width),
      height_(height),
      pixels_(static_cast<size_t>(width) * height * 2, 0) {}

void LcdCanvas::SetPixel(int x, int y, uint8_t r, uint8_t g, uint8_t b) {
  if (x < 0 || y < 0 || x >= width_ || y >= height_) return;
  uint16_t value = RgbToRgb565(r, g, b);
  size_t offset = (static_cast<size_t>(y) * width_ + x) * 2;
  pixels_[offset] = static_cast<uint8_t>(value >> 8);
  pixels_[offset + 1] = static_cast<uint8_t>(value & 0xFF);
}

void LcdCanvas::Clear(uint8_t r, uint8_t g, uint8_t b) {
  FillRect(0, 0, width_, height_, r, g, b);
}

void LcdCanvas::FillRect(int x, int y, int w, int h, uint8_t r, uint8_t g,
                          uint8_t b) {
  int x0 = std::max(0, x);
  int y0 = std::max(0, y);
  int x1 = std::min(width_, x + w);
  int y1 = std::min(height_, y + h);
  uint16_t value = RgbToRgb565(r, g, b);
  uint8_t hi = static_cast<uint8_t>(value >> 8);
  uint8_t lo = static_cast<uint8_t>(value & 0xFF);
  for (int py = y0; py < y1; ++py) {
    size_t row_offset = (static_cast<size_t>(py) * width_) * 2;
    for (int px = x0; px < x1; ++px) {
      size_t offset = row_offset + static_cast<size_t>(px) * 2;
      pixels_[offset] = hi;
      pixels_[offset + 1] = lo;
    }
  }
}

void LcdCanvas::DrawRect(int x, int y, int w, int h, uint8_t r, uint8_t g,
                          uint8_t b) {
  FillRect(x, y, w, 1, r, g, b);
  FillRect(x, y + h - 1, w, 1, r, g, b);
  FillRect(x, y, 1, h, r, g, b);
  FillRect(x + w - 1, y, 1, h, r, g, b);
}

void LcdCanvas::DrawChar(int x, int y, char c, int scale, uint8_t r, uint8_t g,
                          uint8_t b) {
  if (scale < 1) return;
  unsigned char code = static_cast<unsigned char>(c);
  if (code >= 128) return;
  const uint8_t* glyph = kFont8x8Basic[code];
  for (int row = 0; row < 8; ++row) {
    uint8_t bits = glyph[row];
    for (int col = 0; col < 8; ++col) {
      if ((bits >> col) & 0x01) {
        FillRect(x + col * scale, y + row * scale, scale, scale, r, g, b);
      }
    }
  }
}

int LcdCanvas::DrawText(int x, int y, const std::string& text, int scale,
                          uint8_t r, uint8_t g, uint8_t b) {
  int cursor_x = x;
  for (char c : text) {
    DrawChar(cursor_x, y, c, scale, r, g, b);
    cursor_x += 8 * scale;
  }
  return cursor_x - x;
}

void LcdCanvas::DrawCharShinonomeAscii(int x, int y, char c, int scale,
                                        uint8_t r, uint8_t g, uint8_t b) {
  if (scale < 1) return;
  unsigned char code = static_cast<unsigned char>(c);
  if (code < kShinonomeAsciiFirst || code > kShinonomeAsciiLast) return;
  const uint8_t* glyph = kShinonomeAscii16[code - kShinonomeAsciiFirst];
  for (int row = 0; row < 16; ++row) {
    uint8_t bits = glyph[row];
    for (int col = 0; col < 8; ++col) {
      if ((bits >> col) & 0x01) {
        FillRect(x + col * scale, y + row * scale, scale, scale, r, g, b);
      }
    }
  }
}

void LcdCanvas::DrawKanji16(int x, int y, char32_t codepoint, int scale,
                             uint8_t r, uint8_t g, uint8_t b) {
  if (scale < 1) return;
  const uint16_t* rows = nullptr;
  for (size_t i = 0; i < kKanjiGlyphCount; ++i) {
    if (kKanjiGlyphs16[i].codepoint == codepoint) {
      rows = kKanjiGlyphs16[i].rows;
      break;
    }
  }
  if (rows == nullptr) {
    DrawRect(x, y, 16 * scale, 16 * scale, r, g, b);
    return;
  }
  for (int row = 0; row < 16; ++row) {
    uint16_t bits = rows[row];
    for (int col = 0; col < 16; ++col) {
      if ((bits >> col) & 0x01) {
        FillRect(x + col * scale, y + row * scale, scale, scale, r, g, b);
      }
    }
  }
}

int LcdCanvas::DrawTextUtf8(int x, int y, const std::string& utf8_text,
                             int scale, uint8_t r, uint8_t g, uint8_t b) {
  int cursor_x = x;
  size_t pos = 0;
  while (pos < utf8_text.size()) {
    char32_t cp = DecodeUtf8At(utf8_text, pos);
    if (cp <= 0x7F) {
      DrawCharShinonomeAscii(cursor_x, y, static_cast<char>(cp), scale, r, g,
                              b);
      cursor_x += 8 * scale;
    } else {
      DrawKanji16(cursor_x, y, cp, scale, r, g, b);
      cursor_x += 16 * scale;
    }
  }
  return cursor_x - x;
}

int LcdCanvas::MeasureUtf8Width(const std::string& utf8_text, int scale) {
  int width = 0;
  size_t pos = 0;
  while (pos < utf8_text.size()) {
    char32_t cp = DecodeUtf8At(utf8_text, pos);
    width += (cp <= 0x7F) ? 8 * scale : 16 * scale;
  }
  return width;
}

}  // namespace mk2
