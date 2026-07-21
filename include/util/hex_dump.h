// Hex-dump helpers, used by --dry-run logging and the test programs under
// tests/. Mirrors the Python project's protocol.hex_dump/preview_hex_dump so
// dry-run output stays easy to eyeball-compare against the original tool.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace mk2util {

// One line per `width` bytes: "OFFS: b0 b1 b2 ...".
std::string HexDump(const std::vector<uint8_t>& data, size_t width = 16,
                     size_t start_offset = 0);

// For large buffers (e.g. a full 480x272 LCD packet), dumps the first `head`
// bytes and last `tail` bytes with an "omitted N bytes" marker between them,
// so dry-run logs stay readable instead of printing ~130KB of pixel data.
std::string PreviewHexDump(const std::vector<uint8_t>& data, size_t head = 256,
                            size_t tail = 32);

}  // namespace mk2util
