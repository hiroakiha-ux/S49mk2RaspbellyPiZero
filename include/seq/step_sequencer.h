// Minimal internal step sequencer.
//
// One fixed-length pattern per SEQTRAK track (see seqtrak::kTracks), driven
// by an internal clock thread. This is intentionally a scaffold: 16 steps,
// one note per step, no swing/automation/per-step probability yet. It exists
// so the MK2 (pads/knobs, once wired up in the app layer) can program and
// trigger patterns without needing a DAW; note events are emitted through a
// caller-supplied callback so the app can route them via Mk2SeqtrakRouter.
#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>

#include "seqtrak_protocol.h"

namespace mk2seq {

constexpr int kStepsPerPattern = 16;

struct Step {
  bool active = false;
  uint8_t note = 36;       // MIDI note number
  uint8_t velocity = 100;  // 1-127
  uint8_t gate_percent = 80;  // percent of the step duration to hold the note
};

struct TrackPattern {
  std::array<Step, kStepsPerPattern> steps{};
};

// Emitted when the sequencer wants a note turned on or off on a given
// SEQTRAK track (0-based index into seqtrak::kTracks).
struct NoteEvent {
  int track_index;
  bool note_on;
  uint8_t note;
  uint8_t velocity;
};

class StepSequencer {
 public:
  using NoteEventCallback = std::function<void(const NoteEvent&)>;

  StepSequencer();
  ~StepSequencer();

  StepSequencer(const StepSequencer&) = delete;
  StepSequencer& operator=(const StepSequencer&) = delete;

  void SetNoteEventCallback(NoteEventCallback callback);

  void SetTempoBpm(double bpm);
  double TempoBpm() const { return tempo_bpm_.load(); }

  // Starts/stops the internal clock thread. Stop() sends note-off for any
  // currently-held notes before returning.
  void Start();
  void Stop();
  bool IsRunning() const { return running_.load(); }

  // Pattern editing, thread-safe. `track_index` is 0-based into
  // seqtrak::kTracks (0-10).
  void SetStep(int track_index, int step_index, Step step);
  Step GetStep(int track_index, int step_index) const;
  void ClearTrack(int track_index);
  void ClearAll();

  int current_step() const { return current_step_.load(); }

 private:
  void ClockLoop();
  void TriggerStep(int step_index);

  std::array<TrackPattern, seqtrak::kTrackCount> patterns_;
  mutable std::mutex patterns_mutex_;

  std::atomic<double> tempo_bpm_{120.0};
  std::atomic<bool> running_{false};
  std::atomic<int> current_step_{0};
  std::thread clock_thread_;

  NoteEventCallback note_event_callback_;

  // Notes currently sounding, so Stop()/step-advance can send matching
  // note-offs (index = track).
  std::array<bool, seqtrak::kTrackCount> notes_held_{};
  std::array<uint8_t, seqtrak::kTrackCount> held_note_number_{};
};

}  // namespace mk2seq
