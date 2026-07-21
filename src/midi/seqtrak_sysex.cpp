#include "midi/seqtrak_sysex.h"

namespace mk2 {

namespace {

std::vector<uint8_t> BuildEnvelope(uint8_t type_nibble, int device_number,
                                    const seqtrak::ParamAddress& address,
                                    const std::vector<uint8_t>& data) {
  std::vector<uint8_t> msg = {
      seqtrak::kSysExStart,
      seqtrak::kYamahaManufacturerId,
      static_cast<uint8_t>(type_nibble | (device_number & 0x0F)),
      seqtrak::kSysExGroupHigh,
      seqtrak::kSysExGroupLow,
      seqtrak::kSysExModelId,
      address.high,
      address.mid,
      address.low,
  };
  msg.insert(msg.end(), data.begin(), data.end());
  msg.push_back(seqtrak::kSysExEnd);
  return msg;
}

}  // namespace

std::vector<uint8_t> BuildSeqtrakParameterChange(
    int device_number, const seqtrak::ParamAddress& address,
    const std::vector<uint8_t>& data) {
  return BuildEnvelope(seqtrak::kSysExTypeParameterChange, device_number,
                        address, data);
}

std::vector<uint8_t> BuildSeqtrakParameterRequest(
    int device_number, const seqtrak::ParamAddress& address) {
  return BuildEnvelope(seqtrak::kSysExTypeParameterRequest, device_number,
                        address, {});
}

std::vector<uint8_t> BuildSeqtrakDumpRequest(
    int device_number, const seqtrak::ParamAddress& address) {
  return BuildEnvelope(seqtrak::kSysExTypeDumpRequest, device_number, address,
                        {});
}

std::optional<SeqtrakParameterReply> ParseSeqtrakParameterChange(
    const std::vector<uint8_t>& bytes) {
  // Header (9 bytes) + at least 1 data byte + F7 terminator = 11 bytes min.
  if (bytes.size() < 11) return std::nullopt;
  if (bytes.front() != seqtrak::kSysExStart) return std::nullopt;
  if (bytes.back() != seqtrak::kSysExEnd) return std::nullopt;
  if (bytes[1] != seqtrak::kYamahaManufacturerId) return std::nullopt;
  if ((bytes[2] & 0xF0) != seqtrak::kSysExTypeParameterChange) {
    return std::nullopt;
  }
  if (bytes[3] != seqtrak::kSysExGroupHigh) return std::nullopt;
  if (bytes[4] != seqtrak::kSysExGroupLow) return std::nullopt;
  if (bytes[5] != seqtrak::kSysExModelId) return std::nullopt;

  SeqtrakParameterReply reply;
  reply.address = {bytes[6], bytes[7], bytes[8]};
  reply.data.assign(bytes.begin() + 9, bytes.end() - 1);
  return reply;
}

}  // namespace mk2
