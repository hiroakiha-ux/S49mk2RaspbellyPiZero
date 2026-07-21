#include "seq/step_sequencer.h"

#include <algorithm>
#include <chrono>

namespace mk2seq {

namespace {
constexpr int kTrackCount = static_cast<int>(seqtrak::kTrackCount);
}  // namespace

StepSequencer::StepSequencer() = default;

StepSequencer::~StepSequencer() { Stop(); }

void StepSequencer::SetNoteEventCallback(NoteEventCallback callback) {
  note_event_callback_ = std::move(callback);
}

void StepSequencer::SetTempoBpm(double bpm) {
  tempo_bpm_.store(std::clamp(bpm, 20.0, 300.0));
}

void StepSequencer::SetStep(int track_index, int step_index, Step step) {
  if (track_index < 0 || track_index >= kTrackCount) return;
  if (step_index < 0 || step_index >= kStepsPerPattern) return;
  std::lock_guard<std::mutex> lock(patterns_mutex_);
  patterns_[track_index].steps[step_index] = step;
}

Step StepSequencer::GetStep(int track_index, int step_index) const {
  if (track_index < 0 || track_index >= kTrackCount) return Step{};
  if (step_index < 0 || step_index >= kStepsPerPattern) return Step{};
  std::lock_guard<std::mutex> lock(patterns_mutex_);
  return patterns_[track_index].steps[step_index];
}

void StepSequencer::ClearTrack(int track_index) {
  if (track_index < 0 || track_index >= kTrackCount) return;
  std::lock_guard<std::mutex> lock(patterns_mutex_);
  patterns_[track_index] = TrackPattern{};
}

void StepSequencer::ClearAll() {
  std::lock_guard<std::mutex> lock(patterns_mutex_);
  for (auto& pattern : patterns_) pattern = TrackPattern{};
}

void StepSequencer::Start() {
  if (running_.exchange(true)) return;
  current_step_.store(0);
  clock_thread_ = std::thread([this] { ClockLoop(); });
}

void StepSequencer::Stop() {
  if (!running_.exchange(false)) return;
  if (clock_thread_.joinable()) clock_thread_.join();

  // Flush any notes still held.
  for (int track = 0; track < kTrackCount; ++track) {
    if (notes_held_[track] && note_event_callback_) {
      note_event_callback_(
          NoteEvent{track, false, held_note_number_[track], 0});
    }
    notes_held_[track] = false;
  }
}

void StepSequencer::TriggerStep(int step_index) {
  std::array<Step, seqtrak::kTrackCount> steps_snapshot;
  {
    std::lock_guard<std::mutex> lock(patterns_mutex_);
    for (int track = 0; track < kTrackCount; ++track) {
      steps_snapshot[track] = patterns_[track].steps[step_index];
    }
  }

  const double bpm = tempo_bpm_.load();
  const auto step_duration =
      std::chrono::duration<double>(60.0 / bpm / 4.0);  // 16th notes

  // Note-on phase: turn off any still-held note (overlap guard), then start
  // newly active steps.
  for (int track = 0; track < kTrackCount; ++track) {
    if (notes_held_[track] && note_event_callback_) {
      note_event_callback_(
          NoteEvent{track, false, held_note_number_[track], 0});
      notes_held_[track] = false;
    }
    const Step& step = steps_snapshot[track];
    if (step.active && note_event_callback_) {
      note_event_callback_(NoteEvent{track, true, step.note, step.velocity});
      notes_held_[track] = true;
      held_note_number_[track] = step.note;
    }
  }

  // Hold for the gated portion of the step (per-step gate_percent, capped to
  // the longest active gate this step so shorter-gated tracks still get an
  // accurate note-off; simple scaffold, not per-track independent timing).
  int max_gate_percent = 0;
  for (int track = 0; track < kTrackCount; ++track) {
    if (steps_snapshot[track].active) {
      max_gate_percent =
          std::max<int>(max_gate_percent, steps_snapshot[track].gate_percent);
    }
  }
  if (max_gate_percent > 0) {
    auto gate_duration = step_duration * (max_gate_percent / 100.0);
    std::this_thread::sleep_for(gate_duration);
    for (int track = 0; track < kTrackCount; ++track) {
      if (notes_held_[track] && note_event_callback_) {
        note_event_callback_(
            NoteEvent{track, false, held_note_number_[track], 0});
        notes_held_[track] = false;
      }
    }
    std::this_thread::sleep_for(step_duration - gate_duration);
  } else {
    std::this_thread::sleep_for(step_duration);
  }
}

void StepSequencer::ClockLoop() {
  while (running_.load()) {
    int step_index = current_step_.load();
    TriggerStep(step_index);
    if (!running_.load()) break;
    current_step_.store((step_index + 1) % kStepsPerPattern);
  }
}

}  // namespace mk2seq
