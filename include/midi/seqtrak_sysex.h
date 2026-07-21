// SEQTRAK System Exclusive message builders/parser.
//
// Envelope per SEQTRAK_data_list_En_D0.pdf, MIDI Data Format (3-5-2) through
// (3-5-5), pages 111-116:
//
//   Parameter Change:  F0 43 1n 7F 1C 0C ah am al dd [dd...] F7
//   Bulk Dump:         F0 43 0n 7F 1C bh bl 0C ah am al dd [dd...] cc F7
//   Dump Request:      F0 43 2n 7F 1C 0C ah am al F7
//   Parameter Request: F0 43 3n 7F 1C 0C ah am al F7
//
// A Parameter Request is answered with a Parameter Change message carrying
// the current value at that address; a Dump Request is answered with a Bulk
// Dump of the whole block. This module only covers Parameter Change/Request
// (single-parameter read/write) -- Bulk Dump is not implemented yet.
#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "seqtrak_protocol.h"

namespace mk2 {

// device_number is the low nibble `n` (0 selects "device 1"; SEQTRAK ignores
// this in practice for a single connected unit).
std::vector<uint8_t> BuildSeqtrakParameterChange(
    int device_number, const seqtrak::ParamAddress& address,
    const std::vector<uint8_t>& data);

std::vector<uint8_t> BuildSeqtrakParameterRequest(
    int device_number, const seqtrak::ParamAddress& address);

std::vector<uint8_t> BuildSeqtrakDumpRequest(int device_number,
                                              const seqtrak::ParamAddress& address);

struct SeqtrakParameterReply {
  seqtrak::ParamAddress address;
  std::vector<uint8_t> data;
};

// Parses a complete F0..F7 message as a SEQTRAK Parameter Change (the reply
// to a Parameter Request, or a message we're about to send). Returns
// nullopt if `bytes` isn't a well-formed SEQTRAK Parameter Change envelope.
std::optional<SeqtrakParameterReply> ParseSeqtrakParameterChange(
    const std::vector<uint8_t>& bytes);

}  // namespace mk2
