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
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
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
  ControllerApp();
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
  enum class ScreenId {
    kControllerHome,
    kSoundSelect,
    kDrumSoundCategory,
    kSynthSoundCategory,
    kDxSoundCategory,
    kSamplerSoundCategory,
    kDrumKit,
    kSoundList,
    kSettings,
    kSeqtrakTrackSelect,
    kFmEditor,
    kAwm2Editor,
    kKeySplit,
    kSetCcPc,
    kMidiLog
  };
  enum class ActionId { kPlay, kSoundSelect, kSetting };
  enum class LcdUiMode { kTrackSelect, kTrackTypeSelect, kTrackDetail };
  struct ControlAssignment {
    int cc = 0;
  };

  struct KeySplitZone {
    int low_note = 24;    // Standard MIDI C1
    int high_note = 127;  // Standard MIDI G9
    int midi_channel = 1;
    int transpose = 0;  // Signed semitones, -64..+63
    int color = 0;
  };

  struct KeySplitSettings {
    int zone_count = 1;
    std::array<KeySplitZone, 16> zones{};
  };

  void DrawStartupScreens();
  void DrawLeftLcdUi();
  void DrawControllerHome(mk2::LcdCanvas& canvas);
  void DrawSoundDestination(mk2::LcdCanvas& canvas);
  void DrawDrumKit(mk2::LcdCanvas& canvas);
  void DrawVariationSummary(mk2::LcdCanvas& canvas);
  void DrawSoundList(mk2::LcdCanvas& canvas, bool right_screen);
  void DrawSettings(mk2::LcdCanvas& canvas);
  void DrawSeqtrakTrackSelect(mk2::LcdCanvas& canvas);
  void DrawFmEditor(mk2::LcdCanvas& canvas, bool right_screen);
  void DrawAwm2Editor(mk2::LcdCanvas& canvas, bool right_screen);
  void DrawKeySplit(mk2::LcdCanvas& canvas);
  void DrawSetCcPc(mk2::LcdCanvas& canvas, bool right_screen);
  void DrawMidiLog(mk2::LcdCanvas& canvas);
  void PollHidLoop();
  void HandleHidReport(const std::vector<uint8_t>& report);
  void OnMk2MidiMessage(const mk2::MidiMessage& message);
  void ApplyPendingUiAction();
  void ActivateAction(ActionId action);
  void ReturnToControllerHome();
  void OpenSelectedSoundDestination();
  void ReturnToVariation();
  bool ApplySelectedSound();
  bool SendSoundPreset(int kind, uint16_t number, int channel,
                       int drum_kit_part = -1);
  bool ApplyAllTrackSounds();
  void AppendMidiLog(const mk2::MidiMessage& message);
  void ApplyPendingMidiLogRedraw();
  void ApplyPendingMidiControls();
  void MoveJogSelection(int delta);
  void ConfirmJogSelection();
  void MoveKeySplitRow(int delta);
  void MoveKeySplitColumn(int delta);
  void ChangeKeySplitValue(int delta);
  void MoveSetCcPcRow(int delta);
  void MoveSetCcPcColumn(int delta);
  void ChangeSetCcPcValue(int delta);
  bool ApplyControlAssignments(
      const std::array<ControlAssignment, 17>& assignments);
  bool LoadControlAssignments();
  bool SaveControlAssignments(
      const std::array<ControlAssignment, 17>& assignments);
  bool SaveFmPatch();
  void NormalizeKeySplitRanges(KeySplitSettings& settings);
  void LoadDrumKeySplitPreset();
  void LoadDrumKitKeySplitPreset();
  std::vector<uint8_t> BuildKeySplitReport(
      const KeySplitSettings& settings) const;
  bool ApplyKeySplitSettings(const KeySplitSettings& settings);
  void OnSequencerNoteEvent(const mk2seq::NoteEvent& event);

  std::unique_ptr<mk2::HidDevice> hid_device_;
  std::unique_ptr<mk2::LcdBulkDevice> lcd_device_;
  std::unique_ptr<mk2::AlsaRawMidiPort> mk2_midi_port_;
  std::unique_ptr<mk2::AlsaRawMidiPort> seqtrak_midi_port_;
  std::unique_ptr<mk2::Mk2SeqtrakRouter> router_;
  std::unique_ptr<mk2seq::StepSequencer> sequencer_;

  std::vector<uint8_t> previous_hid_report_;
  ScreenId current_screen_ = ScreenId::kControllerHome;
  int selected_home_button_ = 0;
  int selected_settings_item_ = 0;
  int selected_seqtrak_track_ = -1;  // -1=Prev., 0..10=track
  int fm_page_ = 0;
  int selected_fm_header_action_ = 1;  // 0=Prev., 1=Save
  std::array<uint8_t, 28> fm_common_{};
  std::array<std::array<uint8_t, 36>, 4> fm_operators_{};
  std::string fm_status_;
  int awm_page_ = 0;
  int awm_track_ = 0;
  enum class AwmOverviewMode { kOverview, kCategory, kSound };
  AwmOverviewMode awm_overview_mode_ = AwmOverviewMode::kOverview;
  int selected_awm_element_ = 0;
  int selected_awm_category_ = 0;
  int selected_awm_sound_ = 0;
  std::array<int, 8> awm_element_categories_{};
  std::array<uint16_t, 8> awm_element_sound_numbers_{};
  std::array<std::string, 8> awm_element_sound_names_{};
  std::array<std::array<uint8_t, 8>, 5> awm_common_{};
  std::array<std::array<std::array<uint8_t, 8>, 4>, 8> awm_elements_{};
  std::array<std::array<uint8_t, 8>, 2> awm_insertions_{};
  int selected_dialog_action_ = 0;
  std::array<ControlAssignment, 17> control_assignments_{};
  std::array<ControlAssignment, 17> edited_control_assignments_{};
  int selected_ccpc_row_ = -1;
  int selected_ccpc_column_ = 0;
  int selected_ccpc_action_ = 0;
  std::string ccpc_status_;
  KeySplitSettings key_split_settings_{};
  KeySplitSettings edited_key_split_settings_{};
  // -1 is the header (OK/Cancel), 0 is Zones, 1..16 are Zone rows. Zone-row
  // columns are End, Channel, Transpose, Color; Start is derived/read-only.
  int selected_key_split_row_ = -1;
  int selected_key_split_column_ = 0;
  int selected_key_split_action_ = 0;
  int selected_key_split_zones_action_ = 0;
  LcdUiMode lcd_ui_mode_ = LcdUiMode::kTrackSelect;
  int selected_track_ = 0;
  int selected_variation_item_ = 1;  // 0=Prev., 1..11=track, 12=OK
  int selected_drum_kit_item_ = 0;   // 0=Prev., 1=Type, 2..8=parts, 9=OK
  int selected_sound_category_item_ = 0;  // 0=Prev., 1..15=category
  int selected_sound_kind_ = 0;  // 0=Drum, 1=Synth, 2=DX, 3=SAMPLER
  int selected_sound_category_index_ = 0;
  int selected_sound_list_item_ = -1;  // -1=Prev., 0..N-1=sound
  bool sound_list_for_drum_kit_ = false;
  int selected_drum_kit_part_ = 0;
  std::array<std::string, 11> track_sound_names_{};
  std::array<std::string, 11> track_sound_categories_{};
  std::array<uint16_t, 11> track_sound_numbers_{};
  std::array<int, 11> track_sound_kinds_ = {-1, -1, -1, -1, -1, -1,
                                            -1, -1, -1, -1, -1};
  std::array<std::string, 7> drum_kit_sound_names_{};
  std::array<uint16_t, 7> drum_kit_sound_numbers_{};
  std::string variation_status_;
  int selected_track_type_ = 0;
  std::array<int, 11> track_types_{};
  std::array<int, 11> track_volumes_ = {30, 30, 30, 30, 30, 30,
                                        30, 30, 30, 30, 30};
  std::array<int, 11> track_pans_{};
  // The MK2 can send a knob's value update after a separate touch-OFF
  // report, so remember which physical knob most recently became active.
  int last_touched_knob_ = -1;
  std::chrono::steady_clock::time_point last_knob2_touch_{};
  std::array<std::atomic<int>, 8> pending_knob_cc_{
      std::atomic<int>{-1}, std::atomic<int>{-1}, std::atomic<int>{-1},
      std::atomic<int>{-1}, std::atomic<int>{-1}, std::atomic<int>{-1},
      std::atomic<int>{-1}, std::atomic<int>{-1}};
  std::atomic<int> pending_ui_action_{-1};
  std::mutex midi_log_mutex_;
  std::deque<std::string> midi_log_lines_;
  std::atomic<bool> midi_log_redraw_pending_{false};
  int last_pan_midi_value_ = -1;
  bool pan_rebased_after_reset_ = false;
  bool right_lcd_has_ui_ = false;
  std::atomic<bool> running_{false};
  bool dry_run_ = false;
};

}  // namespace mk2app
