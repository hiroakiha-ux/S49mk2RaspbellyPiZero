// Builds MK2 LCD bulk-write packets from an LcdCanvas (or a raw RGB565
// buffer), per the packet layout documented in mk2_protocol.h. Ported from
// the Python project's display.py build_lcd_packet().
#pragma once

#include <cstdint>
#include <vector>

#include "display/lcd_canvas.h"
#include "mk2_protocol.h"

namespace mk2 {

// Builds a full-canvas packet targeting (0,0) on the given screen.
// `screen` must be kLcdScreenLeft or kLcdScreenRight.
std::vector<uint8_t> BuildLcdPacket(int screen, const LcdCanvas& canvas);

// Builds a packet for an arbitrary sub-rectangle, given a raw big-endian
// RGB565 pixel buffer of exactly width*height*2 bytes. Throws
// std::invalid_argument if the rectangle falls outside the 480x272 screen or
// the buffer size doesn't match width*height*2.
std::vector<uint8_t> BuildLcdPacket(int screen, int x, int y, int width,
                                     int height,
                                     const std::vector<uint8_t>& rgb565_be);

}  // namespace mk2
