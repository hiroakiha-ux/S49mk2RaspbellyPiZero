// Hardware-free self-test for the LCD packet builder.
//
// Compares mk2::BuildLcdPacket()'s byte output against the exact hex vectors
// asserted by the original Python project's tests/test_display_packet.py
// (which were themselves confirmed against real S49 MK2 hardware). If this
// program prints "ALL TESTS PASSED", the C++ port produces byte-identical
// LCD packets to the tool that was actually verified on real hardware -- no
// MK2, SEQTRAK, or even a Raspberry Pi required.
//
// This is "Phase 0" of the testing plan: run it on your Mac before touching
// any hardware. See ../README.md and ./README.md for the full incremental
// bring-up sequence.
//
// Build (from the repo root):
//   cmake -B build && cmake --build build --target lcd_packet_selftest
//   ./build/lcd_packet_selftest
#include <cstdio>
#include <string>
#include <vector>

#include "display/lcd_canvas.h"
#include "display/lcd_packet.h"
#include "mk2_protocol.h"
#include "util/hex_dump.h"

namespace {

int g_failures = 0;

std::vector<uint8_t> BytesFromHex(const std::string& hex) {
  std::vector<uint8_t> out;
  out.reserve(hex.size() / 2);
  for (size_t i = 0; i + 1 < hex.size(); i += 2) {
    out.push_back(static_cast<uint8_t>(std::stoul(hex.substr(i, 2), nullptr, 16)));
  }
  return out;
}

std::string ToHex(const std::vector<uint8_t>& bytes) {
  std::string out;
  char b[3];
  for (uint8_t byte : bytes) {
    std::snprintf(b, sizeof(b), "%02x", byte);
    out += b;
  }
  return out;
}

void ExpectEqual(const std::string& test_name, const std::vector<uint8_t>& actual,
                  const std::string& expected_hex) {
  std::vector<uint8_t> expected = BytesFromHex(expected_hex);
  if (actual == expected) {
    std::printf("[PASS] %s\n", test_name.c_str());
    return;
  }
  ++g_failures;
  std::printf("[FAIL] %s\n", test_name.c_str());
  std::printf("  expected: %s\n", expected_hex.c_str());
  std::printf("  actual:   %s\n", ToHex(actual).c_str());
}

void ExpectEqual(const std::string& test_name, size_t actual, size_t expected) {
  if (actual == expected) {
    std::printf("[PASS] %s\n", test_name.c_str());
    return;
  }
  ++g_failures;
  std::printf("[FAIL] %s: expected %zu, got %zu\n", test_name.c_str(), expected,
              actual);
}

std::vector<uint8_t> Slice(const std::vector<uint8_t>& data, size_t begin,
                            size_t end) {
  return std::vector<uint8_t>(data.begin() + static_cast<long>(begin),
                               data.begin() + static_cast<long>(end));
}

// ---------------------------------------------------------------------------
// Tests. Each mirrors one test in the Python project's
// tests/test_display_packet.py; expected hex values are copy-pasted from
// there verbatim.
// ---------------------------------------------------------------------------

void TestRgb565Values() {
  ExpectEqual("rgb565: pure red",
              {static_cast<uint8_t>(mk2::RgbToRgb565(255, 0, 0) >> 8),
               static_cast<uint8_t>(mk2::RgbToRgb565(255, 0, 0) & 0xFF)},
              "f800");
  ExpectEqual("rgb565: pure green",
              {static_cast<uint8_t>(mk2::RgbToRgb565(0, 255, 0) >> 8),
               static_cast<uint8_t>(mk2::RgbToRgb565(0, 255, 0) & 0xFF)},
              "07e0");
  ExpectEqual("rgb565: pure blue",
              {static_cast<uint8_t>(mk2::RgbToRgb565(0, 0, 255) >> 8),
               static_cast<uint8_t>(mk2::RgbToRgb565(0, 0, 255) & 0xFF)},
              "001f");
}

void TestHeaderSuffixAndBigEndianPixels() {
  // 2x1 image: red pixel, green pixel; placed at (3,4) on the right screen.
  uint16_t red = mk2::RgbToRgb565(255, 0, 0);
  uint16_t green = mk2::RgbToRgb565(0, 255, 0);
  std::vector<uint8_t> pixels = {
      static_cast<uint8_t>(red >> 8), static_cast<uint8_t>(red & 0xFF),
      static_cast<uint8_t>(green >> 8), static_cast<uint8_t>(green & 0xFF)};

  std::vector<uint8_t> packet =
      mk2::BuildLcdPacket(mk2::kLcdScreenRight, 3, 4, 2, 1, pixels);

  ExpectEqual("header+suffix: header bytes", Slice(packet, 0, 24),
              "840001600000000000030004000200010200000000000001");
  ExpectEqual("header+suffix: pixel bytes", Slice(packet, 24, 28),
              "f80007e0");
  ExpectEqual("header+suffix: suffix bytes",
              Slice(packet, packet.size() - 12, packet.size()),
              "020000000300000040000000");
  ExpectEqual("header+suffix: total length", packet.size(),
              static_cast<size_t>(24 + 4 + 12));
}

void TestFullScreenDefault() {
  mk2::LcdCanvas canvas;  // defaults to 480x272, zero-initialized (black)
  std::vector<uint8_t> packet =
      mk2::BuildLcdPacket(mk2::kLcdScreenLeft, canvas);

  ExpectEqual("full screen: header bytes", Slice(packet, 0, 24),
              "84000060000000000000000001e00110020000000000ff00");
  ExpectEqual(
      "full screen: total length", packet.size(),
      static_cast<size_t>(24 + mk2::kLcdWidth * mk2::kLcdHeight * 2 + 12));
}

void TestSolidNamedColorOrange() {
  // RGB_COLORS["orange"] = (255, 128, 0) in the Python project.
  uint16_t orange = mk2::RgbToRgb565(255, 128, 0);
  std::vector<uint8_t> pixel = {static_cast<uint8_t>(orange >> 8),
                                 static_cast<uint8_t>(orange & 0xFF)};
  std::vector<uint8_t> packet =
      mk2::BuildLcdPacket(mk2::kLcdScreenLeft, 0, 0, 1, 1, pixel);

  ExpectEqual("solid orange: header bytes", Slice(packet, 0, 24),
              "840000600000000000000000000100010200000000000001");
  ExpectEqual("solid orange: pixel bytes", Slice(packet, 24, 26), "fc00");
}

void TestSolidHexColorBlue() {
  // "#0000ff" -> (0, 0, 255)
  uint16_t blue = mk2::RgbToRgb565(0, 0, 255);
  std::vector<uint8_t> pixel = {static_cast<uint8_t>(blue >> 8),
                                 static_cast<uint8_t>(blue & 0xFF)};
  std::vector<uint8_t> packet =
      mk2::BuildLcdPacket(mk2::kLcdScreenRight, 2, 3, 1, 1, pixel);

  ExpectEqual("solid #0000ff: header bytes", Slice(packet, 0, 24),
              "840001600000000000020003000100010200000000000001");
  ExpectEqual("solid #0000ff: pixel bytes", Slice(packet, 24, 26), "001f");
}

}  // namespace

int main() {
  std::printf("lcd_packet_selftest: validating BuildLcdPacket() against "
              "Python-project-verified vectors\n\n");

  TestRgb565Values();
  TestHeaderSuffixAndBigEndianPixels();
  TestFullScreenDefault();
  TestSolidNamedColorOrange();
  TestSolidHexColorBlue();

  std::printf("\n");
  if (g_failures == 0) {
    std::printf("ALL TESTS PASSED\n");
    return 0;
  }
  std::printf("%d TEST(S) FAILED\n", g_failures);
  return 1;
}
