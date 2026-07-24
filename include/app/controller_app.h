// Top-level application: owns the MK2 HID/display connections, the SEQTRAK
// MIDI connection, the MK2<->SEQTRAK router, and the internal step
// sequencer. This is the minimal wiring described in the project brief:
//   - MK2 knobs/Function buttons -> SEQTRAK Control Change (default map)
//   - MK2 keybed <-> SEQTRAK MIDI, relayed bidirectionally
//   - internal step sequencer notes -> SEQTRAK, per track/channel
//   - a static demo frame pushed to both MK2 LCDs on startup
#pragma once

#include <atomic>
#include <array>
#include <chrono>
#include <memory>
#include <optional>
#include <vector>

#include "display/lcd_canvas.h"
#include "midi/alsa_rawmidi_port.h"
#include "midi/router.h"
#include "seq/step_sequencer.h"
#include "usb/bulk_display_device.h"
#include "usb/hid_device.h"

namespace mk2app {

class ControllerApp {
 public:
  ControllerApp() = default;
  ~ControllerApp();

  ControllerApp(const ControllerApp&) = delete;
  ControllerApp& operator=(const ControllerApp&) = delete;

  // Must be called before Initialize(). When true, no bytes are ever
  // written to the MK2 or SEQTRAK: LCD packets are built and previewed
  // (hex dump) instead of sent over the bulk endpoint, and all MIDI
  // traffic (relay pass-through and app-generated CC/notes) is logged to
  // stderr instead of written. Devices are still opened/read so the whole
  // pipeline can be exercised safely. See README.md / tests/README.md for
  // the recommended incremental hardware bring-up using this flag.
  void SetDryRun(bool dry_run) { dry_run_ = dry_run; }

  // Opens the MK2 HID interface, the MK2 LCD bulk endpoint, and both ALSA
  // rawmidi ports (MK2 USB-MIDI, SEQTRAK). Returns false and logs to stderr
  // on the first failure; partial functionality (e.g. no LCD) is tolerated
  // where noted in the implementation.
  bool Initialize();

  // Runs the HID poll loop, router, and sequencer until Stop() is called
  // from another thread (e.g. a signal handler).
  void Run();
  void Stop();

 private:
  enum class LcdUiMode { kTrackSelect, kTrackTypeSelect, kTrackDetail };

  void DrawStartupScreens();
  void DrawLeftLcdUi();
  void PollHidLoop();
  void HandleHidReport(const std::vector<uint8_t>& report);
  void OnMk2MidiMessage(const mk2::MidiMessage& message);
  void ApplyPendingMidiControls();
  void MoveJogSelection(int delta);
  void ConfirmJogSelection();
  void OnSequencerNoteEvent(const mk2seq::NoteEvent& event);

  std::unique_ptr<mk2::HidDevice> hid_device_;
  std::unique_ptr<mk2::LcdBulkDevice> lcd_device_;
  std::unique_ptr<mk2::AlsaRawMidiPort> mk2_midi_port_;
  std::unique_ptr<mk2::AlsaRawMidiPort> seqtrak_midi_port_;
  std::unique_ptr<mk2::Mk2SeqtrakRouter> router_;
  std::unique_ptr<mk2seq::StepSequencer> sequencer_;

  std::vector<uint8_t> previous_hid_report_;
  LcdUiMode lcd_ui_mode_ = LcdUiMode::kTrackSelect;
  int selected_track_ = 0;
  int selected_track_type_ = 0;
  std::array<int, 11> track_types_{};
  std::array<int, 11> track_volumes_ = {30, 30, 30, 30, 30, 30,
                                        30, 30, 30, 30, 30};
  std::array<int, 11> track_pans_{};
  // The MK2 can send a knob's value update after a separate touch-OFF
  // report, so remember which physical knob most recently became active.
  int last_touched_knob_ = -1;
  std::chrono::steady_clock::time_point last_knob2_touch_{};
  std::array<std::atomic<int>, 2> pending_knob_cc_{
      std::atomic<int>{-1}, std::atomic<int>{-1}};
  int last_pan_midi_value_ = -1;
  bool pan_rebased_after_reset_ = false;
  std::atomic<bool> running_{false};
  bool dry_run_ = false;
};

}  // namespace mk2app
