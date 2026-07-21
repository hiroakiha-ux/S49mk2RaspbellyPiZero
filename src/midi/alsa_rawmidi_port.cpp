#include "midi/alsa_rawmidi_port.h"

#include <poll.h>

namespace mk2 {

AlsaRawMidiPort::~AlsaRawMidiPort() { Close(); }

bool AlsaRawMidiPort::OpenDevice(const std::string& device_name) {
  Close();
  int err = snd_rawmidi_open(&input_, &output_, device_name.c_str(),
                              SND_RAWMIDI_NONBLOCK);
  if (err < 0) {
    last_error_ = std::string("snd_rawmidi_open(") + device_name +
                  ") failed: " + snd_strerror(err);
    input_ = nullptr;
    output_ = nullptr;
    return false;
  }
  device_name_ = device_name;
  return true;
}

bool AlsaRawMidiPort::OpenByNameSubstring(const std::string& name_substring) {
  int card = -1;
  while (snd_card_next(&card) == 0 && card >= 0) {
    snd_ctl_t* ctl = nullptr;
    std::string ctl_name = "hw:" + std::to_string(card);
    if (snd_ctl_open(&ctl, ctl_name.c_str(), 0) < 0) continue;

    int device = -1;
    while (snd_ctl_rawmidi_next_device(ctl, &device) == 0 && device >= 0) {
      snd_rawmidi_info_t* info = nullptr;
      snd_rawmidi_info_alloca(&info);
      snd_rawmidi_info_set_device(info, device);

      snd_rawmidi_info_set_stream(info, SND_RAWMIDI_STREAM_OUTPUT);
      if (snd_ctl_rawmidi_info(ctl, info) < 0) continue;
      const char* name = snd_rawmidi_info_get_name(info);
      if (name == nullptr) continue;

      std::string name_str(name);
      if (name_str.find(name_substring) == std::string::npos) continue;

      snd_ctl_close(ctl);
      std::string dev_id =
          "hw:" + std::to_string(card) + "," + std::to_string(device);
      return OpenDevice(dev_id);
    }
    snd_ctl_close(ctl);
  }

  last_error_ = "no rawmidi device matching \"" + name_substring + "\" found";
  return false;
}

void AlsaRawMidiPort::Close() {
  if (input_ != nullptr) {
    snd_rawmidi_close(input_);
    input_ = nullptr;
  }
  if (output_ != nullptr) {
    snd_rawmidi_close(output_);
    output_ = nullptr;
  }
}

bool AlsaRawMidiPort::Write(const std::vector<uint8_t>& bytes) {
  if (output_ == nullptr || bytes.empty()) return false;
  ssize_t written = snd_rawmidi_write(output_, bytes.data(), bytes.size());
  if (written < 0) {
    last_error_ = std::string("snd_rawmidi_write failed: ") +
                  snd_strerror(static_cast<int>(written));
    return false;
  }
  return static_cast<size_t>(written) == bytes.size();
}

std::optional<std::vector<uint8_t>> AlsaRawMidiPort::Read(int timeout_ms,
                                                           size_t max_len) {
  if (input_ == nullptr) return std::nullopt;

  int npfds = snd_rawmidi_poll_descriptors_count(input_);
  if (npfds <= 0) return std::nullopt;
  std::vector<pollfd> pfds(static_cast<size_t>(npfds));
  snd_rawmidi_poll_descriptors(input_, pfds.data(), static_cast<unsigned>(npfds));

  int ready = poll(pfds.data(), static_cast<nfds_t>(npfds), timeout_ms);
  if (ready <= 0) return std::nullopt;

  unsigned short revents = 0;
  if (snd_rawmidi_poll_descriptors_revents(input_, pfds.data(),
                                            static_cast<unsigned>(npfds),
                                            &revents) < 0) {
    return std::nullopt;
  }
  if ((revents & POLLIN) == 0) return std::nullopt;

  std::vector<uint8_t> buffer(max_len);
  ssize_t n = snd_rawmidi_read(input_, buffer.data(), buffer.size());
  if (n <= 0) return std::nullopt;
  buffer.resize(static_cast<size_t>(n));
  return buffer;
}

}  // namespace mk2
