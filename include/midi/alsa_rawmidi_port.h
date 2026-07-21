// ALSA rawmidi wrapper (/dev/snd/midiC*D*) for talking to the MK2's
// USB-MIDI endpoint and the YAMAHA SEQTRAK. Deliberately raw-byte level: no
// MIDI parsing here, see midi/router.h for message decoding.
#pragma once

#include <alsa/asoundlib.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace mk2 {

class AlsaRawMidiPort {
 public:
  AlsaRawMidiPort() = default;
  ~AlsaRawMidiPort();

  AlsaRawMidiPort(const AlsaRawMidiPort&) = delete;
  AlsaRawMidiPort& operator=(const AlsaRawMidiPort&) = delete;

  // Opens an explicit ALSA rawmidi device name, e.g. "hw:1,0,0".
  bool OpenDevice(const std::string& device_name);

  // Scans ALSA rawmidi devices (via snd_card_next / snd_rawmidi_info) for one
  // whose card or subdevice name contains `name_substring` (case-sensitive),
  // e.g. "SEQTRAK" or "KOMPLETE KONTROL". Opens the first match.
  bool OpenByNameSubstring(const std::string& name_substring);

  void Close();
  bool IsOpen() const { return input_ != nullptr || output_ != nullptr; }

  // Blocking write of raw MIDI bytes (status + data bytes, or a full SysEx
  // message including F0/F7).
  bool Write(const std::vector<uint8_t>& bytes);

  // Blocking read with a timeout; returns std::nullopt on timeout/error/EOF.
  std::optional<std::vector<uint8_t>> Read(int timeout_ms,
                                            size_t max_len = 256);

  const std::string& device_name() const { return device_name_; }
  const std::string& last_error() const { return last_error_; }

 private:
  snd_rawmidi_t* input_ = nullptr;
  snd_rawmidi_t* output_ = nullptr;
  std::string device_name_;
  std::string last_error_;
};

}  // namespace mk2
