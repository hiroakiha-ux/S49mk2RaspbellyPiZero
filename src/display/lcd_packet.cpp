#include "display/lcd_packet.h"

#include <stdexcept>

namespace mk2 {

namespace {

void AppendBigEndianU16(std::vector<uint8_t>* out, uint16_t value) {
  out->push_back(static_cast<uint8_t>(value >> 8));
  out->push_back(static_cast<uint8_t>(value & 0xFF));
}

}  // namespace

std::vector<uint8_t> BuildLcdPacket(int screen, int x, int y, int width,
                                     int height,
                                     const std::vector<uint8_t>& rgb565_be) {
  if (screen != kLcdScreenLeft && screen != kLcdScreenRight) {
    throw std::invalid_argument("screen must be 0 (left) or 1 (right)");
  }
  if (width <= 0 || height <= 0 || x < 0 || y < 0) {
    throw std::invalid_argument("invalid image rectangle");
  }
  if (x + width > kLcdWidth || y + height > kLcdHeight) {
    throw std::invalid_argument("image rectangle exceeds 480x272 screen");
  }
  const size_t expected_len = static_cast<size_t>(width) * height * 2;
  if (rgb565_be.size() != expected_len) {
    throw std::invalid_argument("pixel buffer size does not match width*height*2");
  }

  std::vector<uint8_t> packet;
  packet.reserve(kLcdHeaderSize + rgb565_be.size() + kLcdSuffixSize);

  packet.insert(packet.end(), kLcdPacketPrefix.begin(), kLcdPacketPrefix.end());
  packet.push_back(static_cast<uint8_t>(screen));
  packet.insert(packet.end(), kLcdHeaderConstantA.begin(),
                kLcdHeaderConstantA.end());
  AppendBigEndianU16(&packet, static_cast<uint16_t>(x));
  AppendBigEndianU16(&packet, static_cast<uint16_t>(y));
  AppendBigEndianU16(&packet, static_cast<uint16_t>(width));
  AppendBigEndianU16(&packet, static_cast<uint16_t>(height));
  packet.insert(packet.end(), kLcdHeaderConstantB.begin(),
                kLcdHeaderConstantB.end());

  // Pixel data length in 32-bit words: ceil(bytes / 4).
  uint16_t image_longs = static_cast<uint16_t>((rgb565_be.size() + 3) >> 2);
  AppendBigEndianU16(&packet, image_longs);

  packet.insert(packet.end(), rgb565_be.begin(), rgb565_be.end());
  packet.insert(packet.end(), kLcdPacketSuffix.begin(), kLcdPacketSuffix.end());

  return packet;
}

std::vector<uint8_t> BuildLcdPacket(int screen, const LcdCanvas& canvas) {
  return BuildLcdPacket(screen, 0, 0, canvas.width(), canvas.height(),
                        canvas.pixels());
}

}  // namespace mk2
