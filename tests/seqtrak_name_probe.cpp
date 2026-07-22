// SEQTRAK preset-name probe.
//
// Tests a specific, testable doubt raised by the user: does selecting a
// factory preset (Bank Select + Program Change) actually populate the
// "Sound Common Name" SysEx block (address {0x31, 0x0p, offset}, p=part,
// offset 0x00-0x63 = up to 100 UTF-8-encoded name characters, MIDI Data
// Table p.139), or does that block stay whatever the user last typed via a
// companion editor (i.e. SEQTRAK itself has no screen to show names, so the
// name might only ever exist inside editor apps, not on the device)?
//
// Selects a program via Bank Select MSB/LSB (CC0/CC32) + Program Change,
// waits briefly, then reads back the first N Sound Common Name bytes via
// repeated Parameter Requests and prints them as hex + ASCII.
//
// Build (Linux/Pi only, needs ALSA -- see CMakeLists.txt):
//   cmake -B build && cmake --build build --target seqtrak_name_probe
//   ./build/seqtrak_name_probe --track 1 --program 1
//   (Program 1 on KICK, preset bank 1, should be "Tight Punchy Kick 1" per
//   SEQTRAK_data_list_En_D0.pdf's Drum Sound List, p.3, if the name really
//   does live on the device.)
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <thread>
#include <vector>

#include "midi/alsa_rawmidi_port.h"
#include "midi/router.h"
#include "midi/seqtrak_sysex.h"
#include "seqtrak_protocol.h"
#include "util/hex_dump.h"

namespace {

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

void PrintUsage(const char* prog) {
  std::fprintf(stderr,
               "Usage: %s --track N --program P [--bank-msb H --bank-lsb L] "
               "[--chars C]\n"
               "  N: 1-%zu. P: 1-128 (sent as Program Change P-1). Default "
               "bank is\n"
               "  Preset bank 1 (MSB 0x3F, LSB 0x00). Reads the first C "
               "(default 20)\n"
               "  Sound Common Name bytes after selecting the program.\n",
               prog, seqtrak::kTrackCount);
}

}  // namespace

int main(int argc, char** argv) {
  int track = 0;
  int program = 0;
  int bank_msb = seqtrak::kBankSoundPresetBase.msb;
  int bank_lsb = seqtrak::kBankSoundPresetBase.lsb;
  int chars = 20;

  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--track") == 0 && i + 1 < argc) {
      track = std::atoi(argv[++i]);
    } else if (std::strcmp(argv[i], "--program") == 0 && i + 1 < argc) {
      program = std::atoi(argv[++i]);
    } else if (std::strcmp(argv[i], "--bank-msb") == 0 && i + 1 < argc) {
      bank_msb = std::atoi(argv[++i]);
    } else if (std::strcmp(argv[i], "--bank-lsb") == 0 && i + 1 < argc) {
      bank_lsb = std::atoi(argv[++i]);
    } else if (std::strcmp(argv[i], "--chars") == 0 && i + 1 < argc) {
      chars = std::atoi(argv[++i]);
    } else {
      PrintUsage(argv[0]);
      return (std::strcmp(argv[i], "--help") == 0 ||
              std::strcmp(argv[i], "-h") == 0)
                 ? 0
                 : 1;
    }
  }

  if (track < 1 || track > static_cast<int>(seqtrak::kTrackCount) ||
      program < 1 || program > 128) {
    std::fprintf(stderr,
                  "seqtrak_name_probe: --track must be 1-%zu, --program "
                  "1-128\n",
                  seqtrak::kTrackCount);
    PrintUsage(argv[0]);
    return 1;
  }

  int part = track - 1;
  int channel = seqtrak::kTracks[part].midi_channel;

  mk2::AlsaRawMidiPort port;
  if (!port.OpenByNameSubstring("SEQTRAK")) {
    std::fprintf(stderr, "seqtrak_name_probe: %s\n", port.last_error().c_str());
    return 1;
  }
  std::fprintf(stderr, "seqtrak_name_probe: opened %s\n",
               port.device_name().c_str());
  std::fprintf(stderr,
               "seqtrak_name_probe: track=%d (%s) channel=%d bank_msb=%d "
               "bank_lsb=%d program=%d\n",
               track, seqtrak::kTracks[part].name, channel, bank_msb,
               bank_lsb, program);

  port.Write(mk2::BuildControlChange(channel, seqtrak::kCcBankSelectMsb,
                                       static_cast<uint8_t>(bank_msb)));
  port.Write(mk2::BuildControlChange(channel, seqtrak::kCcBankSelectLsb,
                                       static_cast<uint8_t>(bank_lsb)));
  port.Write(mk2::BuildProgramChange(channel, static_cast<uint8_t>(program - 1)));
  std::fprintf(stderr, "seqtrak_name_probe: sent Bank Select + Program Change, "
                       "waiting 300ms\n");
  std::this_thread::sleep_for(std::chrono::milliseconds(300));

  std::vector<uint8_t> name_bytes;
  for (int offset = 0; offset < chars; ++offset) {
    seqtrak::ParamAddress address{
        0x31, static_cast<uint8_t>(seqtrak::kAddrSoundCommonNameMidBase | part),
        static_cast<uint8_t>(offset)};
    auto request = mk2::BuildSeqtrakParameterRequest(/*device_number=*/0, address);
    if (!port.Write(request)) {
      std::fprintf(stderr, "seqtrak_name_probe: write failed at offset %d: %s\n",
                    offset, port.last_error().c_str());
      return 1;
    }
    auto reply_bytes = ReadSysExReply(port, /*overall_timeout_ms=*/3000);
    if (!reply_bytes.has_value()) {
      std::fprintf(stderr,
                    "seqtrak_name_probe: no reply at offset %d after 3s, "
                    "stopping\n",
                    offset);
      break;
    }
    auto parsed = mk2::ParseSeqtrakParameterChange(*reply_bytes);
    if (!parsed.has_value() || parsed->data.empty()) {
      std::fprintf(stderr,
                    "seqtrak_name_probe: unparsable reply at offset %d\n",
                    offset);
      break;
    }
    name_bytes.push_back(parsed->data[0]);
  }

  std::fprintf(stderr, "seqtrak_name_probe: raw bytes:\n%s",
               mk2util::HexDump(name_bytes).c_str());

  std::printf("seqtrak_name_probe: name (as text) = \"");
  for (uint8_t b : name_bytes) {
    std::putchar((b >= 0x20 && b < 0x7F) ? static_cast<char>(b) : '.');
  }
  std::printf("\"\n");

  bool all_zero = true;
  for (uint8_t b : name_bytes) {
    if (b != 0x00) {
      all_zero = false;
      break;
    }
  }
  std::printf("seqtrak_name_probe: %s\n",
              all_zero
                  ? "ALL ZERO -- the name block does not appear to be "
                    "auto-populated by Program Change"
                  : "non-zero data present -- name block does appear to be "
                    "populated");

  return 0;
}
