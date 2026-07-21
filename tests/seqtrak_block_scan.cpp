// SEQTRAK SysEx block scan.
//
// Sends a Parameter Request for one representative address in every
// top-level parameter block from SEQTRAK_data_list_En_D0.pdf's "Parameter
// Base Address" master table (p.118), waiting up to --timeout-ms (default
// 10000, per the user's "10秒経っても返ってこないか" request) for a reply to
// each one, and prints a pass/fail + latency summary. This exists because an
// earlier probe tool's 1.5s timeout was mistaken for several blocks
// genuinely not replying (see seqtrak_param_probe.cpp's header comment) --
// this scan checks every block once with a generous wait to settle, once and
// for all, which blocks (if any) truly never reply.
//
// Only ever sends Parameter Request (read), never Parameter Change/Bulk
// Dump, so it cannot alter any stored SEQTRAK setting.
//
// Build (Linux/Pi only, needs ALSA -- see CMakeLists.txt):
//   cmake -B build && cmake --build build --target seqtrak_block_scan
//   ./build/seqtrak_block_scan
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <vector>

#include "midi/alsa_rawmidi_port.h"
#include "midi/seqtrak_sysex.h"
#include "seqtrak_protocol.h"
#include "util/hex_dump.h"

namespace {

struct BlockEntry {
  const char* name;
  seqtrak::ParamAddress address;
};

// One representative address per top-level block, p.118. Where a block only
// accepts certain part numbers (see seqtrak_protocol.h comments), a valid
// part is used instead of always defaulting to part 0.
constexpr BlockEntry kBlocks[] = {
    {"System General", {0x00, 0x00, 0x00}},
    {"Format Version", {0x00, 0x7F, 0x00}},
    {"Bulk Header", {0x11, 0x00, 0x00}},
    {"Bulk Footer", {0x12, 0x00, 0x00}},
    {"Project Common General", {0x30, 0x40, 0x00}},
    {"Project Common Send Reverb", {0x30, 0x41, 0x00}},
    {"Project Common Send Delay", {0x30, 0x42, 0x00}},
    {"Project Common Master Effect 1", {0x30, 0x43, 0x00}},
    {"Project Common Master Effect 2", {0x30, 0x44, 0x00}},
    {"Project Common Master Effect 3", {0x30, 0x45, 0x00}},
    {"Project Common Master Effect 4", {0x30, 0x46, 0x00}},
    {"Project Common Master EQ", {0x30, 0x47, 0x00}},
    {"Project Common A/D Insertion A", {0x30, 0x49, 0x00}},
    {"Project Common A/D Insertion B", {0x30, 0x4A, 0x00}},
    {"Project Common A/D General", {0x30, 0x4B, 0x00}},
    {"Project Common USB Audio Input", {0x30, 0x4C, 0x00}},
    {"Project Common Scale", {0x30, 0x4D, 0x00}},
    {"Project Track General (KICK, part0)", {0x30, 0x50, 0x00}},
    {"Project Track Chord Notes 1-4 (SYNTH1, part7)", {0x30, 0x67, 0x00}},
    {"Project Track Chord Notes 5-8 (SYNTH1, part7)", {0x30, 0x77, 0x00}},
    {"Sound Common Name (KICK, part0)", {0x31, 0x00, 0x00}},
    {"Sound Common General (KICK, part0)", {0x31, 0x10, 0x00}},
    {"Sound Common Insertion A (KICK, part0)", {0x31, 0x20, 0x00}},
    {"Sound Common Insertion B (KICK, part0)", {0x31, 0x30, 0x00}},
    {"Sound Common LFO (KICK, part0)", {0x31, 0x40, 0x00}},
    {"Sound Common Arpeggiator (SYNTH1, part7)", {0x31, 0x57, 0x00}},
    {"Sound Element Osc/Amp/Pitch (KICK part0, elem0)", {0x41, 0x00, 0x00}},
    {"Sound Element Filter/EQ/LFO (KICK part0, elem0)", {0x42, 0x00, 0x00}},
    {"Sound DX Common", {0x48, 0x09, 0x00}},
    {"Sound DX Operator1", {0x49, 0x09, 0x00}},
    {"SAMPLER Sample General (elem0)", {0x50, 0x0A, 0x00}},
};

bool IsSystemRealtimeByte(uint8_t b) {
  return b == 0xF8 || b == 0xFA || b == 0xFB || b == 0xFC || b == 0xFE;
}

std::optional<std::vector<uint8_t>> ReadSysExReply(mk2::AlsaRawMidiPort& port,
                                                     int overall_timeout_ms) {
  std::vector<uint8_t> collected;
  bool started = false;
  auto deadline = std::chrono::steady_clock::now() +
                   std::chrono::milliseconds(overall_timeout_ms);
  while (std::chrono::steady_clock::now() < deadline) {
    auto chunk = port.Read(/*timeout_ms=*/200);
    if (!chunk.has_value()) continue;
    for (uint8_t b : *chunk) {
      if (IsSystemRealtimeByte(b)) continue;
      if (!started) {
        if (b != 0xF0) continue;
        started = true;
      }
      collected.push_back(b);
      if (started && b == 0xF7) return collected;
    }
  }
  return std::nullopt;
}

}  // namespace

int main(int argc, char** argv) {
  int timeout_ms = 10000;
  int device_number = 0;

  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--timeout-ms") == 0 && i + 1 < argc) {
      timeout_ms = std::atoi(argv[++i]);
    } else if (std::strcmp(argv[i], "--device") == 0 && i + 1 < argc) {
      device_number = std::atoi(argv[++i]);
    } else {
      std::fprintf(stderr,
                    "Usage: %s [--timeout-ms MS] [--device N]\n"
                    "  Scans every top-level SEQTRAK parameter block with a "
                    "Parameter Request,\n"
                    "  waiting up to MS (default 10000) per block.\n",
                    argv[0]);
      return (std::strcmp(argv[i], "--help") == 0 ||
              std::strcmp(argv[i], "-h") == 0)
                 ? 0
                 : 1;
    }
  }

  mk2::AlsaRawMidiPort port;
  if (!port.OpenByNameSubstring("SEQTRAK")) {
    std::fprintf(stderr, "seqtrak_block_scan: %s\n",
                 port.last_error().c_str());
    return 1;
  }
  std::fprintf(stderr, "seqtrak_block_scan: opened %s, timeout=%dms/block\n\n",
               port.device_name().c_str(), timeout_ms);

  int ok_count = 0;
  int timeout_count = 0;

  for (const auto& block : kBlocks) {
    auto request =
        mk2::BuildSeqtrakParameterRequest(device_number, block.address);
    auto start = std::chrono::steady_clock::now();
    if (!port.Write(request)) {
      std::printf("%-52s address=%02x %02x %02x  WRITE FAILED: %s\n",
                  block.name, block.address.high, block.address.mid,
                  block.address.low, port.last_error().c_str());
      continue;
    }

    auto reply_bytes = ReadSysExReply(port, timeout_ms);
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::steady_clock::now() - start)
                          .count();

    if (!reply_bytes.has_value()) {
      std::printf("%-52s address=%02x %02x %02x  TIMEOUT after %lldms\n",
                  block.name, block.address.high, block.address.mid,
                  block.address.low, static_cast<long long>(elapsed_ms));
      ++timeout_count;
      continue;
    }

    auto parsed = mk2::ParseSeqtrakParameterChange(*reply_bytes);
    if (!parsed.has_value()) {
      std::printf(
          "%-52s address=%02x %02x %02x  REPLY (unparsed, %zu bytes) in "
          "%lldms\n",
          block.name, block.address.high, block.address.mid,
          block.address.low, reply_bytes->size(),
          static_cast<long long>(elapsed_ms));
      ++ok_count;
      continue;
    }

    std::printf("%-52s address=%02x %02x %02x  OK data=",
                block.name, block.address.high, block.address.mid,
                block.address.low);
    for (uint8_t b : parsed->data) std::printf("%02x ", b);
    std::printf("(%lldms)\n", static_cast<long long>(elapsed_ms));
    ++ok_count;
  }

  std::printf("\n%d/%zu blocks replied, %d timed out (at %dms)\n", ok_count,
              sizeof(kBlocks) / sizeof(kBlocks[0]), timeout_count, timeout_ms);

  return 0;
}
