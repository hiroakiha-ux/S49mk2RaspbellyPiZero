#include "midi/router.h"

#include <cstdio>

#include "util/hex_dump.h"

namespace mk2 {

namespace {

constexpr int kPumpPollTimeoutMs = 50;

}  // namespace

MidiMessage DecodeMidiMessage(const std::vector<uint8_t>& bytes) {
  MidiMessage msg;
  msg.raw = bytes;
  if (bytes.empty()) return msg;

  uint8_t status = bytes[0];
  if (status == 0xF0) {
    msg.kind = MidiMessageKind::kSystemExclusive;
    return msg;
  }
  if (status < 0x80) {
    msg.kind = MidiMessageKind::kOther;
    return msg;
  }

  uint8_t type = status & 0xF0;
  msg.channel = (status < 0xF0) ? ((status & 0x0F) + 1) : 0;
  int data1 = bytes.size() > 1 ? bytes[1] : 0;
  int data2 = bytes.size() > 2 ? bytes[2] : 0;

  switch (type) {
    case 0x80:
      msg.kind = MidiMessageKind::kNoteOff;
      msg.data1 = data1;
      msg.data2 = data2;
      break;
    case 0x90:
      msg.kind = (data2 == 0) ? MidiMessageKind::kNoteOff
                               : MidiMessageKind::kNoteOn;
      msg.data1 = data1;
      msg.data2 = data2;
      break;
    case 0xB0:
      msg.kind = MidiMessageKind::kControlChange;
      msg.data1 = data1;
      msg.data2 = data2;
      break;
    case 0xC0:
      msg.kind = MidiMessageKind::kProgramChange;
      msg.data1 = data1;
      break;
    case 0xE0:
      msg.kind = MidiMessageKind::kPitchBend;
      msg.data1 = data1 | (data2 << 7);
      break;
    default:
      msg.kind = MidiMessageKind::kOther;
      break;
  }
  return msg;
}

std::string FormatMidiMessage(const MidiMessage& message) {
  char line[128];
  switch (message.kind) {
    case MidiMessageKind::kNoteOn:
      std::snprintf(line, sizeof(line), "note-on ch=%d note=%d velocity=%d",
                    message.channel, message.data1, message.data2);
      break;
    case MidiMessageKind::kNoteOff:
      std::snprintf(line, sizeof(line), "note-off ch=%d note=%d velocity=%d",
                    message.channel, message.data1, message.data2);
      break;
    case MidiMessageKind::kControlChange:
      std::snprintf(line, sizeof(line), "cc ch=%d cc=%d value=%d",
                    message.channel, message.data1, message.data2);
      break;
    case MidiMessageKind::kProgramChange:
      std::snprintf(line, sizeof(line), "program-change ch=%d program=%d",
                    message.channel, message.data1);
      break;
    case MidiMessageKind::kPitchBend:
      std::snprintf(line, sizeof(line), "pitch-bend ch=%d value=%d",
                    message.channel, message.data1);
      break;
    case MidiMessageKind::kSystemExclusive:
      std::snprintf(line, sizeof(line), "sysex (%zu bytes)",
                    message.raw.size());
      break;
    default:
      std::snprintf(line, sizeof(line), "other (%zu bytes)",
                    message.raw.size());
      break;
  }
  return std::string(line);
}

std::vector<uint8_t> BuildNoteOn(int channel_1based, uint8_t note,
                                  uint8_t velocity) {
  uint8_t status = static_cast<uint8_t>(0x90 | ((channel_1based - 1) & 0x0F));
  return {status, static_cast<uint8_t>(note & 0x7F),
          static_cast<uint8_t>(velocity & 0x7F)};
}

std::vector<uint8_t> BuildNoteOff(int channel_1based, uint8_t note,
                                   uint8_t velocity) {
  uint8_t status = static_cast<uint8_t>(0x80 | ((channel_1based - 1) & 0x0F));
  return {status, static_cast<uint8_t>(note & 0x7F),
          static_cast<uint8_t>(velocity & 0x7F)};
}

std::vector<uint8_t> BuildControlChange(int channel_1based, uint8_t cc,
                                         uint8_t value) {
  uint8_t status = static_cast<uint8_t>(0xB0 | ((channel_1based - 1) & 0x0F));
  return {status, static_cast<uint8_t>(cc & 0x7F),
          static_cast<uint8_t>(value & 0x7F)};
}

std::vector<uint8_t> BuildProgramChange(int channel_1based, uint8_t program) {
  uint8_t status = static_cast<uint8_t>(0xC0 | ((channel_1based - 1) & 0x0F));
  return {status, static_cast<uint8_t>(program & 0x7F)};
}

Mk2SeqtrakRouter::Mk2SeqtrakRouter(AlsaRawMidiPort* mk2_port,
                                   AlsaRawMidiPort* seqtrak_port,
                                   bool dry_run)
    : mk2_port_(mk2_port), seqtrak_port_(seqtrak_port), dry_run_(dry_run) {
  if (dry_run_) {
    std::fprintf(stderr,
                  "router: dry-run mode, no bytes will be written to MK2 or "
                  "SEQTRAK\n");
  }
}

Mk2SeqtrakRouter::~Mk2SeqtrakRouter() { Stop(); }

void Mk2SeqtrakRouter::Start() {
  if (running_.exchange(true)) return;
  if (mk2_port_ != nullptr) {
    mk2_to_seqtrak_thread_ = std::thread([this] {
      PumpLoop(mk2_port_, seqtrak_port_, "SEQTRAK", &on_mk2_to_seqtrak_,
               &seqtrak_write_mutex_);
    });
  }
  if (seqtrak_port_ != nullptr) {
    seqtrak_to_mk2_thread_ = std::thread([this] {
      PumpLoop(seqtrak_port_, mk2_port_, "MK2", &on_seqtrak_to_mk2_,
               &mk2_write_mutex_);
    });
  }
}

void Mk2SeqtrakRouter::Stop() {
  if (!running_.exchange(false)) return;
  if (mk2_to_seqtrak_thread_.joinable()) mk2_to_seqtrak_thread_.join();
  if (seqtrak_to_mk2_thread_.joinable()) seqtrak_to_mk2_thread_.join();
}

bool Mk2SeqtrakRouter::SendToSeqtrak(const std::vector<uint8_t>& bytes) {
  return WriteOrLog(seqtrak_port_, "SEQTRAK", &seqtrak_write_mutex_, bytes);
}

bool Mk2SeqtrakRouter::SendToMk2(const std::vector<uint8_t>& bytes) {
  return WriteOrLog(mk2_port_, "MK2", &mk2_write_mutex_, bytes);
}

void Mk2SeqtrakRouter::SetOnMk2ToSeqtrakMessage(MessageCallback callback) {
  on_mk2_to_seqtrak_ = std::move(callback);
}

void Mk2SeqtrakRouter::SetOnSeqtrakToMk2Message(MessageCallback callback) {
  on_seqtrak_to_mk2_ = std::move(callback);
}

bool Mk2SeqtrakRouter::WriteOrLog(AlsaRawMidiPort* to, const char* to_label,
                                   std::mutex* to_write_mutex,
                                   const std::vector<uint8_t>& bytes) {
  MidiMessage decoded = DecodeMidiMessage(bytes);
  if (dry_run_) {
    std::fprintf(stderr, "[dry-run] -> %s: %s | %s", to_label,
                 FormatMidiMessage(decoded).c_str(),
                 mk2util::HexDump(bytes).c_str());
    return true;
  }
  // Observation-only mode: keep reading and invoking the callback even when
  // the destination device (normally SEQTRAK) is not connected.
  if (to == nullptr) return true;
  std::lock_guard<std::mutex> lock(*to_write_mutex);
  return to->Write(bytes);
}

void Mk2SeqtrakRouter::PumpLoop(AlsaRawMidiPort* from, AlsaRawMidiPort* to,
                                 const char* to_label,
                                 MessageCallback* callback,
                                 std::mutex* to_write_mutex) {
  while (running_.load()) {
    auto bytes = from->Read(kPumpPollTimeoutMs);
    if (!bytes.has_value()) continue;

    WriteOrLog(to, to_label, to_write_mutex, *bytes);

    if (*callback) {
      (*callback)(DecodeMidiMessage(*bytes));
    }
  }
}

}  // namespace mk2
