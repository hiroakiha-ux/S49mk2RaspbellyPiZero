#include "util/hex_dump.h"

#include <cstdio>

namespace mk2util {

std::string HexDump(const std::vector<uint8_t>& data, size_t width,
                     size_t start_offset) {
  std::string out;
  char line[32];  // "%04zx: " is >= 7 chars; margin for a wider size_t offset
  for (size_t offset = 0; offset < data.size(); offset += width) {
    size_t end = std::min(data.size(), offset + width);
    std::snprintf(line, sizeof(line), "%04zx: ", start_offset + offset);
    out += line;
    for (size_t i = offset; i < end; ++i) {
      char byte_str[4];
      std::snprintf(byte_str, sizeof(byte_str), "%02x ", data[i]);
      out += byte_str;
    }
    out += '\n';
  }
  return out;
}

std::string PreviewHexDump(const std::vector<uint8_t>& data, size_t head,
                            size_t tail) {
  if (data.size() <= head + tail) {
    return HexDump(data);
  }
  size_t omitted = data.size() - head - tail;
  std::string out = HexDump(std::vector<uint8_t>(data.begin(), data.begin() + head));
  char marker[64];
  std::snprintf(marker, sizeof(marker), "... omitted %zu bytes ...\n", omitted);
  out += marker;
  out += HexDump(std::vector<uint8_t>(data.end() - tail, data.end()),
                  16, data.size() - tail);
  return out;
}

}  // namespace mk2util
