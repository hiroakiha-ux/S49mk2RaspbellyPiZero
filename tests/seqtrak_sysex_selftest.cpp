// Hardware-free correctness test for the SEQTRAK SysEx builders/parser
// (midi/seqtrak_sysex.h), checked against hand-computed byte vectors derived
// from SEQTRAK_data_list_En_D0.pdf's documented envelope (MIDI Data Format
// (3-5-2)/(3-5-5), p.111-116) and the Project Track General address (p.134).
// No MIDI/USB hardware involved -- builds and runs on any host.
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "midi/seqtrak_sysex.h"
#include "seqtrak_protocol.h"

namespace {

int g_failures = 0;

void Check(bool condition, const char* what) {
  if (condition) {
    std::printf("[PASS] %s\n", what);
  } else {
    std::printf("[FAIL] %s\n", what);
    ++g_failures;
  }
}

void CheckBytes(const std::vector<uint8_t>& actual,
                 const std::vector<uint8_t>& expected, const char* what) {
  Check(actual == expected, what);
}

void TestParameterRequest() {
  // Track 1 (part 0), Volume (offset 0x00) -> address {0x30, 0x50, 0x00}.
  seqtrak::ParamAddress address =
      seqtrak::ProjectTrackGeneralAddress(/*part=*/0,
                                           seqtrak::kTrackGeneralVolumeOffset);
  CheckBytes({address.high, address.mid, address.low}, {0x30, 0x50, 0x00},
             "Project Track General address for track1/volume");

  auto request = mk2::BuildSeqtrakParameterRequest(/*device_number=*/0, address);
  CheckBytes(request,
             {0xF0, 0x43, 0x30, 0x7F, 0x1C, 0x0C, 0x30, 0x50, 0x00, 0xF7},
             "Parameter Request bytes for track1/volume");
}

void TestParameterChangeAndParse() {
  seqtrak::ParamAddress address =
      seqtrak::ProjectTrackGeneralAddress(/*part=*/0,
                                           seqtrak::kTrackGeneralVolumeOffset);
  auto change =
      mk2::BuildSeqtrakParameterChange(/*device_number=*/0, address, {0x64});
  CheckBytes(
      change,
      {0xF0, 0x43, 0x10, 0x7F, 0x1C, 0x0C, 0x30, 0x50, 0x00, 0x64, 0xF7},
      "Parameter Change bytes for track1/volume=0x64");

  auto parsed = mk2::ParseSeqtrakParameterChange(change);
  Check(parsed.has_value(), "Parameter Change parses successfully");
  if (parsed.has_value()) {
    Check(parsed->address.high == 0x30 && parsed->address.mid == 0x50 &&
              parsed->address.low == 0x00,
          "Parsed address matches");
    CheckBytes(parsed->data, {0x64}, "Parsed data matches");
  }
}

void TestParseRejectsGarbage() {
  Check(!mk2::ParseSeqtrakParameterChange({}).has_value(),
        "empty input rejected");
  Check(!mk2::ParseSeqtrakParameterChange({0x90, 0x40, 0x7F}).has_value(),
        "non-SysEx input rejected");
  // Right envelope but wrong message type nibble (Parameter Request, 0x30,
  // instead of Parameter Change, 0x10) must not parse as a Parameter Change.
  Check(!mk2::ParseSeqtrakParameterChange(
             {0xF0, 0x43, 0x30, 0x7F, 0x1C, 0x0C, 0x30, 0x50, 0x00, 0xF7})
             .has_value(),
        "Parameter Request envelope rejected as Parameter Change");
}

void TestMultiDeviceNumber() {
  seqtrak::ParamAddress address =
      seqtrak::ProjectTrackGeneralAddress(/*part=*/0,
                                           seqtrak::kTrackGeneralVolumeOffset);
  auto request = mk2::BuildSeqtrakParameterRequest(/*device_number=*/3, address);
  Check(request[2] == 0x33, "device_number is packed into the low nibble");
}

}  // namespace

int main() {
  std::printf("seqtrak_sysex_selftest: validating SysEx builders/parser "
              "against SEQTRAK_data_list_En_D0.pdf-derived vectors\n\n");

  TestParameterRequest();
  TestParameterChangeAndParse();
  TestParseRejectsGarbage();
  TestMultiDeviceNumber();

  if (g_failures == 0) {
    std::printf("\nALL TESTS PASSED\n");
    return 0;
  }
  std::printf("\n%d TEST(S) FAILED\n", g_failures);
  return 1;
}
