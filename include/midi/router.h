// Minimal MK2 <-> SEQTRAK MIDI relay.
//
// Two background threads pump raw MIDI bytes bidirectionally between the
// MK2's USB-MIDI ALSA rawmidi port and the SEQTRAK's ALSA rawmidi port, so
// that the S49 MK2 keybed plays SEQTRAK tracks and SEQTRAK's MIDI OUT (e.g.
// clock, or track feedback) reaches the MK2. On top of the raw relay,
// `SendToSeqtrak`/`SendToMk2` let the app layer (control mapper, step
// sequencer) inject additional messages, and `SetOn*Message` callbacks let
// it observe decoded messages flowing through the relay without disturbing
// the pass-through.
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "midi/alsa_rawmidi_port.h"

namespace mk2 {

enum class MidiMessageKind {
  kNoteOff,
  kNoteOn,
  kControlChange,
  kProgramChange,
  kPitchBend,
  kSystemExclusive,
  kOther,
};

struct MidiMessage {
  MidiMessageKind kind = MidiMessageKind::kOther;
  int channel = 0;  // 1-based, 0 if not applicable (e.g. SysEx)
  int data1 = 0;
  int data2 = 0;
  std::vector<uint8_t> raw;
};

// Decodes a single complete MIDI message (status byte + data bytes, or a
// full F0..F7 SysEx run). Running-status-free: `bytes` must start with a
// status byte (>= 0x80).
MidiMessage DecodeMidiMessage(const std::vector<uint8_t>& bytes);

// Human-readable one-line summary, e.g. "note-on ch=1 note=60 velocity=100"
// or "cc ch=1 cc=14 value=64". Used for --dry-run logging.
std::string FormatMidiMessage(const MidiMessage& message);

std::vector<uint8_t> BuildNoteOn(int channel_1based, uint8_t note,
                                  uint8_t velocity);
std::vector<uint8_t> BuildNoteOff(int channel_1based, uint8_t note,
                                   uint8_t velocity = 0);
std::vector<uint8_t> BuildControlChange(int channel_1based, uint8_t cc,
                                         uint8_t value);
std::vector<uint8_t> BuildProgramChange(int channel_1based, uint8_t program);

class Mk2SeqtrakRouter {
 public:
  using MessageCallback = std::function<void(const MidiMessage&)>;

  // When `dry_run` is true, no bytes are ever written to either port: the
  // relay pumps still read both inputs (so the observer callbacks and
  // dry-run log still show real traffic), and `SendToSeqtrak`/`SendToMk2`
  // print what they would have sent (via stderr, hex + decoded summary)
  // instead of calling AlsaRawMidiPort::Write. This mirrors the original
  // Python tool's dry-run-first-then---execute convention.
  Mk2SeqtrakRouter(AlsaRawMidiPort* mk2_port, AlsaRawMidiPort* seqtrak_port,
                    bool dry_run = false);
  ~Mk2SeqtrakRouter();

  Mk2SeqtrakRouter(const Mk2SeqtrakRouter&) = delete;
  Mk2SeqtrakRouter& operator=(const Mk2SeqtrakRouter&) = delete;

  // Starts the two relay pump threads (MK2->SEQTRAK, SEQTRAK->MK2).
  void Start();
  // Signals both pump threads to stop and joins them.
  void Stop();
  bool IsRunning() const { return running_.load(); }

  // Thread-safe injection, independent of the raw relay pumps.
  bool SendToSeqtrak(const std::vector<uint8_t>& bytes);
  bool SendToMk2(const std::vector<uint8_t>& bytes);

  // Observers invoked (on the relay thread) for every message forwarded in
  // each direction. Intended for the step sequencer / LED-feedback layer;
  // keep callbacks fast and non-blocking.
  void SetOnMk2ToSeqtrakMessage(MessageCallback callback);
  void SetOnSeqtrakToMk2Message(MessageCallback callback);

 private:
  void PumpLoop(AlsaRawMidiPort* from, AlsaRawMidiPort* to,
                const char* to_label, MessageCallback* callback,
                std::mutex* to_write_mutex);
  bool WriteOrLog(AlsaRawMidiPort* to, const char* to_label,
                   std::mutex* to_write_mutex,
                   const std::vector<uint8_t>& bytes);

  AlsaRawMidiPort* mk2_port_;
  AlsaRawMidiPort* seqtrak_port_;
  bool dry_run_;

  std::atomic<bool> running_{false};
  std::thread mk2_to_seqtrak_thread_;
  std::thread seqtrak_to_mk2_thread_;

  std::mutex mk2_write_mutex_;
  std::mutex seqtrak_write_mutex_;

  MessageCallback on_mk2_to_seqtrak_;
  MessageCallback on_seqtrak_to_mk2_;
};

}  // namespace mk2
