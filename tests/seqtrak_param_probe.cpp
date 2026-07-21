// SEQTRAK SysEx Parameter Change (write) / Parameter Request (read) probe.
//
// CONFIRMED ON REAL HARDWARE (2026-07-21): Parameter Change (write) works for
// every block tried -- e.g. setting a track's Project Track General Volume
// via SysEx has an immediate, audible effect. Parameter Request (read) also
// works for at least Sound Common General (0x31) -- confirmed via `amidi -S
// ... -d`. Project Track General (0x30) and System General (0x00) showed no
// reply within this tool's original 1.5s timeout, including via raw `amidi`;
// but a Sound Common General address that timed out in *this tool* did reply
// when sent via `amidi` (which waits indefinitely) -- so at least some of the
// earlier "no reply" results may simply have been this tool's timeout being
// too short, not a real protocol limitation. Default timeout is now 8s (see
// --timeout-ms); the 0x30/0x00 "no reply even via amidi" results predate this
// fix and should be re-tested. Read/write priority (user, 2026-07-21): Sound
// Common (0x31), Sound Drum/Synth/SAMPLER Element (0x41/0x42), Sound DX
// (0x48/0x49), SAMPLER Sample (0x50); Project/System blocks deprioritized.
//
// Use --address HH MM LL to probe/write any address directly (hex bytes,
// e.g. --address 31 00 00 for Sound Common Name of track 1), or --track/
// --field for the four Project Track General fields wired up so far.
//
// Formats per SEQTRAK_data_list_En_D0.pdf, MIDI Data Format (3-5-2)/(3-5-5),
// p.111-116; top-level block addresses from the "Parameter Base Address"
// master table, p.118.
//
// Build (Linux/Pi only, needs ALSA -- see CMakeLists.txt):
//   cmake -B build && cmake --build build --target seqtrak_param_probe
//   ./build/seqtrak_param_probe --track 1 --field volume --set 100
//   ./build/seqtrak_param_probe --address 31 10 00        (read attempt)
//   ./build/seqtrak_param_probe --address 31 10 00 --set 5 (write)
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

struct FieldInfo {
  const char* name;
  int offset;
};
constexpr FieldInfo kFields[] = {
    {"volume", seqtrak::kTrackGeneralVolumeOffset},
    {"pan", seqtrak::kTrackGeneralPanOffset},
    {"octave", seqtrak::kTrackGeneralOctaveOffset},
    {"mute", seqtrak::kTrackGeneralMuteOffset},
};

void PrintUsage(const char* prog) {
  std::fprintf(
      stderr,
      "Usage: %s (--track N [--field volume|pan|octave|mute] | --address HH "
      "MM LL)\n"
      "           [--device N] [--timeout-ms MS] (--set VALUE | "
      "[--enable-sysex-transmit])\n"
      "  --timeout-ms MS: how long to wait for a reply (default 8000). A "
      "reply\n"
      "  seen via `amidi -S ... -d` but missed by this tool with the "
      "default\n"
      "  timeout likely just needs a larger value here.\n"
      "  --track N: N is 1-%zu, uses the Project Track General block "
      "(offsets\n"
      "  wired up so far: volume/pan/octave/mute).\n"
      "  --address HH MM LL: raw address bytes in hex, for any block (see "
      "the\n"
      "  file header comment for which blocks are priority / confirmed "
      "read-\n"
      "  capable so far).\n"
      "  --set VALUE: sends a Parameter Change (write) with VALUE (0-127) -- "
      "the\n"
      "  confirmed-working mode for every block tried.\n"
      "  --enable-sysex-transmit: first sends a Parameter Change turning on\n"
      "  System General \"USB MIDI Transmit System Exclusive Message\" "
      "(address\n"
      "  00 00 10) before the Parameter Request.\n"
      "  With neither --set nor --enable-sysex-transmit: sends a Parameter "
      "Request\n"
      "  and waits for a reply (works for some blocks, not others -- see "
      "file\n"
      "  header comment).\n",
      prog, seqtrak::kTrackCount);
}

// System realtime status bytes (0xF8, 0xFA, 0xFB, 0xFC, 0xFE) can appear
// interleaved with any other MIDI data per spec; SEQTRAK also transmits
// Active Sensing (0xFE) every 250ms regardless of what else is happening.
// Drop them while hunting for our F0..F7 reply instead of letting them
// derail message framing.
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
  int track = 0;  // 1-based; 0 = not given
  const char* field_name = "volume";
  int device_number = 0;
  bool enable_sysex_transmit = false;
  bool do_set = false;
  int set_value = 0;
  bool has_raw_address = false;
  seqtrak::ParamAddress raw_address{0, 0, 0};
  int timeout_ms = 8000;

  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--track") == 0 && i + 1 < argc) {
      track = std::atoi(argv[++i]);
    } else if (std::strcmp(argv[i], "--field") == 0 && i + 1 < argc) {
      field_name = argv[++i];
    } else if (std::strcmp(argv[i], "--address") == 0 && i + 3 < argc) {
      has_raw_address = true;
      raw_address.high =
          static_cast<uint8_t>(std::strtol(argv[++i], nullptr, 16));
      raw_address.mid =
          static_cast<uint8_t>(std::strtol(argv[++i], nullptr, 16));
      raw_address.low =
          static_cast<uint8_t>(std::strtol(argv[++i], nullptr, 16));
    } else if (std::strcmp(argv[i], "--device") == 0 && i + 1 < argc) {
      device_number = std::atoi(argv[++i]);
    } else if (std::strcmp(argv[i], "--set") == 0 && i + 1 < argc) {
      do_set = true;
      set_value = std::atoi(argv[++i]);
    } else if (std::strcmp(argv[i], "--enable-sysex-transmit") == 0) {
      enable_sysex_transmit = true;
    } else if (std::strcmp(argv[i], "--timeout-ms") == 0 && i + 1 < argc) {
      timeout_ms = std::atoi(argv[++i]);
    } else if (std::strcmp(argv[i], "--help") == 0 ||
               std::strcmp(argv[i], "-h") == 0) {
      PrintUsage(argv[0]);
      return 0;
    } else {
      std::fprintf(stderr, "seqtrak_param_probe: unknown argument '%s'\n",
                   argv[i]);
      PrintUsage(argv[0]);
      return 1;
    }
  }

  if (do_set && (set_value < 0 || set_value > 127)) {
    std::fprintf(stderr, "seqtrak_param_probe: --set value must be 0-127\n");
    return 1;
  }

  seqtrak::ParamAddress address;
  if (has_raw_address) {
    address = raw_address;
    std::fprintf(stderr, "seqtrak_param_probe: address={%02x %02x %02x}\n",
                 address.high, address.mid, address.low);
  } else {
    if (track < 1 || track > static_cast<int>(seqtrak::kTrackCount)) {
      std::fprintf(stderr,
                   "seqtrak_param_probe: --track must be 1-%zu (or use "
                   "--address)\n",
                   seqtrak::kTrackCount);
      PrintUsage(argv[0]);
      return 1;
    }
    int offset = -1;
    for (const auto& f : kFields) {
      if (std::strcmp(f.name, field_name) == 0) {
        offset = f.offset;
        break;
      }
    }
    if (offset < 0) {
      std::fprintf(stderr, "seqtrak_param_probe: unknown --field '%s'\n",
                   field_name);
      PrintUsage(argv[0]);
      return 1;
    }
    address = seqtrak::ProjectTrackGeneralAddress(track - 1, offset);
    std::fprintf(stderr,
                 "seqtrak_param_probe: track=%d (%s) field=%s "
                 "address={%02x %02x %02x}\n",
                 track, seqtrak::kTracks[track - 1].name, field_name,
                 address.high, address.mid, address.low);
  }

  mk2::AlsaRawMidiPort port;
  if (!port.OpenByNameSubstring("SEQTRAK")) {
    std::fprintf(stderr, "seqtrak_param_probe: %s\n",
                 port.last_error().c_str());
    return 1;
  }
  std::fprintf(stderr, "seqtrak_param_probe: opened %s\n",
               port.device_name().c_str());

  if (do_set) {
    auto change = mk2::BuildSeqtrakParameterChange(
        device_number, address, {static_cast<uint8_t>(set_value)});
    std::fprintf(stderr, "seqtrak_param_probe: -> Parameter Change (set)\n%s",
                 mk2util::HexDump(change).c_str());
    if (!port.Write(change)) {
      std::fprintf(stderr, "seqtrak_param_probe: write failed: %s\n",
                   port.last_error().c_str());
      return 1;
    }
    std::fprintf(stderr, "seqtrak_param_probe: sent (no reply expected)\n");
    return 0;
  }

  if (enable_sysex_transmit) {
    auto enable = mk2::BuildSeqtrakParameterChange(
        device_number,
        seqtrak::ParamAddress{
            seqtrak::kAddrSystemGeneral.high, seqtrak::kAddrSystemGeneral.mid,
            static_cast<uint8_t>(
                seqtrak::kSystemGeneralUsbMidiTransmitSysExOffset)},
        {0x01});
    std::fprintf(stderr,
                 "seqtrak_param_probe: -> enabling USB MIDI Transmit SysEx "
                 "(System General 00 00 %02x = 01)\n%s",
                 seqtrak::kSystemGeneralUsbMidiTransmitSysExOffset,
                 mk2util::HexDump(enable).c_str());
    if (!port.Write(enable)) {
      std::fprintf(stderr, "seqtrak_param_probe: write failed: %s\n",
                   port.last_error().c_str());
      return 1;
    }
  }

  auto request = mk2::BuildSeqtrakParameterRequest(device_number, address);
  std::fprintf(stderr, "seqtrak_param_probe: -> Parameter Request\n%s",
               mk2util::HexDump(request).c_str());
  if (!port.Write(request)) {
    std::fprintf(stderr, "seqtrak_param_probe: write failed: %s\n",
                 port.last_error().c_str());
    return 1;
  }

  auto reply_bytes = ReadSysExReply(port, timeout_ms);
  if (!reply_bytes.has_value()) {
    std::fprintf(
        stderr,
        "seqtrak_param_probe: no SysEx reply within %dms -- check SEQTRAK is "
        "powered on, connected, that its MIDI port name matches \"SEQTRAK\", "
        "or try --timeout-ms with a larger value\n",
        timeout_ms);
    return 1;
  }

  std::fprintf(stderr, "seqtrak_param_probe: <- reply (%zu bytes)\n%s",
               reply_bytes->size(), mk2util::HexDump(*reply_bytes).c_str());

  auto parsed = mk2::ParseSeqtrakParameterChange(*reply_bytes);
  if (!parsed.has_value()) {
    std::fprintf(stderr,
                 "seqtrak_param_probe: reply did not parse as a SEQTRAK "
                 "Parameter Change message\n");
    return 1;
  }

  std::fprintf(stderr, "seqtrak_param_probe: address={%02x %02x %02x} data=",
               parsed->address.high, parsed->address.mid,
               parsed->address.low);
  for (uint8_t b : parsed->data) std::fprintf(stderr, "%02x ", b);
  std::fprintf(stderr, "\n");

  return 0;
}
