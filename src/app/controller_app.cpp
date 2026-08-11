#include "app/controller_app.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <string>

#include "display/lcd_packet.h"
#include "midi/seqtrak_sysex.h"
#include "mk2_protocol.h"
#include "seqtrak_protocol.h"
#include "seqtrak_sound_data.h"
#include "util/hex_dump.h"

namespace mk2app {

namespace {

// Default control channel for MK2 knobs/Function buttons -> SEQTRAK, per
// mk2_protocol.h kDefaultControlsMidiChannel / kDefaultKnobCcBase /
// kDefaultFunctionButtonCcBase. This assumes the MK2 has been programmed
// with the matching 0xA1 assignment (see mk2::kHidReportControlsAssign); the
// HID poll loop below decodes the *observed knob/button report* independent
// of that assignment, so the CC map here is what this bridge chooses to send
// onward to SEQTRAK regardless of how the MK2's own assignment is set.
constexpr int kControlChannel = mk2::kDefaultControlsMidiChannel;
constexpr const char* kTrackTypeNames[] = {"Drum", "Drum Kit", "Synth"};
constexpr int kVolumeMin = 0;
constexpr int kVolumeMax = 100;
constexpr int kPanMin = -50;
constexpr int kPanMax = 50;
constexpr auto kKnobDoubleTouchMin = std::chrono::milliseconds(80);
constexpr auto kKnobDoubleTouchMax = std::chrono::milliseconds(500);
constexpr size_t kMidiLogCapacity = 11;
constexpr const char* kControlAssignmentsPath =
    "s49mk2_control_assignments.conf";
constexpr const char* kFmPatchPath = "seqtrak_dx_patch.json";

struct FmPageSpec {
  const char* title;
  std::array<const char*, 8> labels;
};

constexpr std::array<FmPageSpec, 15> kFmPages = {{
    {"DX FM > ALGORITHM", {"FEEDBACK", "LFO SPD", "LFO PMD", "ALGORITHM", "OP1 LVL", "OP2 LVL", "OP3 LVL", "OP4 LVL"}},
    {"DX FM > OP1 > PITCH", {"ON/OFF", "FREQ MODE", "COARSE", "FINE", "DETUNE", "OUT LVL", "VEL SENS", "FEEDBACK"}},
    {"DX FM > OP1 > EG", {"RATE 1", "RATE 2", "RATE 3", "RATE 4", "LEVEL 1", "LEVEL 2", "LEVEL 3", "LEVEL 4"}},
    {"DX FM > OP1 > MOD", {"KB RATE SC", "KLS-L DEPTH", "KLS-L CURVE", "LFO AMD", "KLS-R DEPTH", "KLS-R CURVE", "LFO>PMD", "PEG ON/OFF"}},
    {"DX FM > OP2 > PITCH", {"ON/OFF", "FREQ MODE", "COARSE", "FINE", "DETUNE", "OUT LVL", "VEL SENS", "FEEDBACK"}},
    {"DX FM > OP2 > EG", {"RATE 1", "RATE 2", "RATE 3", "RATE 4", "LEVEL 1", "LEVEL 2", "LEVEL 3", "LEVEL 4"}},
    {"DX FM > OP2 > MOD", {"KB RATE SC", "KLS-L DEPTH", "KLS-L CURVE", "LFO AMD", "KLS-R DEPTH", "KLS-R CURVE", "LFO>PMD", "PEG ON/OFF"}},
    {"DX FM > OP3 > PITCH", {"ON/OFF", "FREQ MODE", "COARSE", "FINE", "DETUNE", "OUT LVL", "VEL SENS", "FEEDBACK"}},
    {"DX FM > OP3 > EG", {"RATE 1", "RATE 2", "RATE 3", "RATE 4", "LEVEL 1", "LEVEL 2", "LEVEL 3", "LEVEL 4"}},
    {"DX FM > OP3 > MOD", {"KB RATE SC", "KLS-L DEPTH", "KLS-L CURVE", "LFO AMD", "KLS-R DEPTH", "KLS-R CURVE", "LFO>PMD", "PEG ON/OFF"}},
    {"DX FM > OP4 > PITCH", {"ON/OFF", "FREQ MODE", "COARSE", "FINE", "DETUNE", "OUT LVL", "VEL SENS", "FEEDBACK"}},
    {"DX FM > OP4 > EG", {"RATE 1", "RATE 2", "RATE 3", "RATE 4", "LEVEL 1", "LEVEL 2", "LEVEL 3", "LEVEL 4"}},
    {"DX FM > OP4 > MOD", {"KB RATE SC", "KLS-L DEPTH", "KLS-L CURVE", "LFO AMD", "KLS-R DEPTH", "KLS-R CURVE", "LFO>PMD", "PEG ON/OFF"}},
    {"DX FM > COMMON > LFO", {"PB SENS", "LFO WAVE", "LFO SPEED", "LFO DELAY", "LFO PMD", "-", "-", "-"}},
    {"DX FM > COMMON > PEG", {"PEG RATE1", "PEG RATE2", "PEG RATE3", "PEG RATE4", "PEG LVL1", "PEG LVL2", "PEG LVL3", "PEG LVL4"}},
}};

struct AwmPageSpec {
  std::string title;
  std::array<const char*, 8> labels;
};

AwmPageSpec GetAwmPageSpec(int page) {
  constexpr std::array<std::array<const char*, 8>, 5> common = {{
      {"E1", "E2", "E3", "E4", "VOLUME", "PAN", "MONO/POLY", "PORTA TIME"},
      {"VOLUME", "PAN", "MONO/POLY", "PORTA TIME", "PB RANGE+", "REVERB SEND", "VAR SEND", "DRY LEVEL"},
      {"AEG ATTACK", "AEG DECAY", "AEG SUSTAIN", "AEG RELEASE", "FEG ATTACK", "FEG DECAY", "FEG SUSTAIN", "FEG RELEASE"},
      {"CUTOFF OFS", "RESO OFS", "NOTE SHIFT", "LEGATO SLOPE", "KEY ASSIGN", "TRIG/GATE", "PB RANGE-", "-"},
      {"LFO WAVE", "LFO SPEED", "LFO DELAY", "FADE IN", "BOX1 DEST", "BOX1 DEPTH", "BOX2 DEST", "BOX2 DEPTH"},
  }};
  constexpr const char* common_titles[] = {
      "AWM2 SYNTH > OVERVIEW", "AWM2 SYNTH > COMMON > MAIN",
      "AWM2 SYNTH > COMMON > EG", "AWM2 SYNTH > COMMON > FILTER",
      "AWM2 SYNTH > LFO"};
  if (page < 5) return {common_titles[page], common[page]};
  if (page < 37) {
    const int element = (page - 5) / 4 + 1;
    const int section = (page - 5) % 4;
    constexpr const char* names[] = {"BASIC", "ZONE", "AEG", "FILTER"};
    constexpr std::array<std::array<const char*, 8>, 4> labels = {{
        {"ASSIGN", "WAVE NO.", "COARSE", "FINE", "PAN", "LEVEL", "VEL SENS", "OUTPUT"},
        {"NOTE LOW", "NOTE HIGH", "VEL LOW", "VEL HIGH", "LVL BP1", "LVL BP2", "LVL BP3", "LVL BP4"},
        {"ATTACK", "DECAY 1", "DECAY 2", "RELEASE", "ATTACK LVL", "DECAY1 LVL", "DECAY2 LVL", "INIT LVL"},
        {"FILTER TYPE", "CUTOFF", "RESONANCE", "HPF CUTOFF", "FEG DEPTH", "FEG ATTACK", "FEG DECAY", "FEG RELEASE"},
    }};
    return {"AWM2 SYNTH > E" + std::to_string(element) + " > " + names[section],
            labels[section]};
  }
  if (page == 37)
    return {"AWM2 SYNTH > INSERTION A", {"FX TYPE", "RATE", "DEPTH", "FEEDBACK", "WIDTH", "DRY/WET", "-", "-"}};
  return {"AWM2 SYNTH > INSERTION B", {"FX TYPE", "FILTER TYPE", "CUTOFF", "RESONANCE", "-", "DRY/WET", "-", "-"}};
}
constexpr int kKeySplitFieldsPerZone = 4;
constexpr int kKeySplitLowestNote = 24;    // C1
constexpr int kKeySplitHighestNote = 127;  // G9

constexpr std::array<const char*, 15> kDrumCategories = {
    "Kick",          "Snare",  "Rim",   "Clap", "Snap",
    "Closed HiHat",  "Open HiHat", "Shaker / Tambourine", "Ride", "Crash",
    "Tom",           "Bell",   "Conga / Bongo", "World", "SFX"};
constexpr std::array<const char*, 15> kSynthCategories = {
    "Bass",    "Synth Lead", "Piano",    "Keyboard", "Organ",
    "Pad",     "Strings",    "Brass",    "Woodwind", "Guitar",
    "World",   "Mallet",     "Bell",     "Rhythmic", "SFX"};
constexpr std::array<const char*, 15> kDxCategories = kSynthCategories;
constexpr std::array<const char*, 15> kSamplerCategories = {
    "Vocal Count", "Vocal Phrase / Chant", "Singing Vocal",
    "Robotic Vocal / Effect", "Riser", "Laser / Sci-Fi", "Impact",
    "Noise / Distorted Sound", "Ambient / Soundscape", "SFX", "Scratch",
    "Nature / Animals", "Hit / Stab / Musical Instrument Sound",
    "Percussion", "Recorded Sound"};

struct ZoneColor {
  uint8_t r;
  uint8_t g;
  uint8_t b;
  std::array<uint8_t, 2> hid;
};

constexpr ZoneColor kZoneColors[] = {
    {66, 165, 245, mk2::kZoneColorBlue},
    {239, 83, 80, mk2::kZoneColorRed},
    {255, 152, 0, mk2::kZoneColorOrange},
    {102, 187, 106, mk2::kZoneColorGreen},
    {255, 202, 40, mk2::kZoneColorYellow},
    {38, 208, 161, mk2::kZoneColorMint},
    {171, 71, 188, mk2::kZoneColorPurple},
    {0, 0, 0, mk2::kZoneColorOff},
};

struct HomeButtonSpec {
  const char* label;
  int x;
  int width;
};

constexpr HomeButtonSpec kHomeButtons[] = {
    {"Play", 12, 144},
    {"Sound Select", 168, 144},
    {"Setting", 324, 144},
};

int WrappedDelta(int previous, int current, int modulo) {
  int delta = current - previous;
  if (delta > modulo / 2) delta -= modulo;
  if (delta < -modulo / 2) delta += modulo;
  return delta;
}

void DrawLine(mk2::LcdCanvas& canvas, int x0, int y0, int x1, int y1,
              uint8_t r, uint8_t g, uint8_t b) {
  const int dx = std::abs(x1 - x0);
  const int sx = x0 < x1 ? 1 : -1;
  const int dy = -std::abs(y1 - y0);
  const int sy = y0 < y1 ? 1 : -1;
  int error = dx + dy;
  while (true) {
    canvas.FillRect(x0 - 1, y0 - 1, 3, 3, r, g, b);
    if (x0 == x1 && y0 == y1) break;
    const int twice_error = 2 * error;
    if (twice_error >= dy) {
      error += dy;
      x0 += sx;
    }
    if (twice_error <= dx) {
      error += dx;
      y0 += sy;
    }
  }
}

std::vector<std::string> WrapCategoryLabel(const std::string& label) {
  constexpr size_t kMaxCharsPerLine = 17;
  std::vector<std::string> lines;
  size_t start = 0;
  while (start < label.size() && lines.size() < 3) {
    size_t remaining = label.size() - start;
    if (remaining <= kMaxCharsPerLine || lines.size() == 2) {
      lines.push_back(label.substr(start));
      break;
    }
    size_t split = label.rfind(' ', start + kMaxCharsPerLine);
    if (split == std::string::npos || split < start) {
      split = start + kMaxCharsPerLine;
    }
    lines.push_back(label.substr(start, split - start));
    start = split;
    while (start < label.size() && label[start] == ' ') ++start;
  }
  return lines;
}

std::string Ellipsize(const std::string& text, int max_width) {
  if (mk2::LcdCanvas::MeasureUtf8Width(text, 1) <= max_width) return text;
  std::string shortened = text;
  while (!shortened.empty() &&
         mk2::LcdCanvas::MeasureUtf8Width(shortened + "...", 1) > max_width) {
    shortened.pop_back();
  }
  return shortened + "...";
}

const char* CategoryNameForKind(int kind, int index) {
  index = std::clamp(index, 0, 14);
  switch (kind) {
    case 0:
      return kDrumCategories[index];
    case 1:
      return kSynthCategories[index];
    case 2:
      return kDxCategories[index];
    case 3:
      return kSamplerCategories[index];
    default:
      return "";
  }
}

std::vector<const seqtrak::SoundPreset*> FilteredSoundPresets(
    int kind, int category_index) {
  std::vector<const seqtrak::SoundPreset*> result;
  const std::string category = CategoryNameForKind(kind, category_index);
  const auto append_matches = [&result, &category](const auto& presets) {
    for (const auto& preset : presets) {
      if (preset.category == category) result.push_back(&preset);
    }
  };
  switch (kind) {
    case 0:
      append_matches(seqtrak::kDrumSoundPresets);
      break;
    case 1:
      append_matches(seqtrak::kSynthSoundPresets);
      break;
    case 2:
      append_matches(seqtrak::kDXSoundPresets);
      break;
    case 3:
      append_matches(seqtrak::kSamplerSoundPresets);
      break;
    default:
      break;
  }
  return result;
}

void FillRoundedRect(mk2::LcdCanvas& canvas, int x, int y, int w, int h,
                     int radius, uint8_t r, uint8_t g, uint8_t b) {
  if (w <= 0 || h <= 0) return;
  radius = std::min({radius, w / 2, h / 2});
  canvas.FillRect(x + radius, y, w - 2 * radius, h, r, g, b);
  canvas.FillRect(x, y + radius, w, h - 2 * radius, r, g, b);
  for (int inset = 1; inset <= radius; ++inset) {
    canvas.FillRect(x + radius - inset, y + inset - 1,
                    w - 2 * (radius - inset), 1, r, g, b);
    canvas.FillRect(x + radius - inset, y + h - inset,
                    w - 2 * (radius - inset), 1, r, g, b);
  }
}

void DrawShinonomeText(mk2::LcdCanvas& canvas, int x, int y,
                       const std::string& text, uint8_t r, uint8_t g,
                       uint8_t b) {
  canvas.DrawTextUtf8(x, y, text, 1, r, g, b);
}

bool EventActive(const std::vector<uint8_t>& report,
                  const mk2::ButtonInputEvent& event) {
  if (static_cast<size_t>(event.byte_index) >= report.size()) return false;
  uint8_t value = report[event.byte_index];
  if (event.match == mk2::ButtonMatch::kMask) {
    return (value & event.value) == event.value;
  }
  return value == event.value;
}

std::string MidiNoteName(int note) {
  constexpr const char* kNames[] = {
      "C",  "C#", "D",  "D#", "E",  "F",
      "F#", "G",  "G#", "A",  "A#", "B"};
  if (note < 0 || note > 127) return std::to_string(note);
  return std::string(kNames[note % 12]) + std::to_string(note / 12 - 1);
}

std::string KeySplitNoteName(int note) {
  constexpr const char* kNames[] = {
      "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
  note = std::clamp(note, 0, 127);
  return std::string(kNames[note % 12]) + std::to_string(note / 12 - 1);
}

std::string ExtendedNoteName(int note) {
  constexpr const char* kNames[] = {
      "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
  // C++ division truncates toward zero; adjust to mathematical floor so
  // negative extended note numbers retain the expected pitch class.
  int octave_division = note / 12;
  int pitch_class = note % 12;
  if (pitch_class < 0) {
    pitch_class += 12;
    --octave_division;
  }
  return std::string(kNames[pitch_class]) +
         std::to_string(octave_division - 1);
}

std::string FormatKeySplitTranspose(int semitones, int zone_start_note) {
  std::string amount = semitones > 0 ? "+" + std::to_string(semitones)
                                     : std::to_string(semitones);
  return amount + "(" +
         ExtendedNoteName(zone_start_note + semitones) + ")";
}

std::string FormatMidiLogLine(const mk2::MidiMessage& message) {
  char line[64];
  switch (message.kind) {
    case mk2::MidiMessageKind::kNoteOn:
      std::snprintf(line, sizeof(line), "%02d  NOTE ON   %-3s V:%03d",
                    message.channel, MidiNoteName(message.data1).c_str(),
                    message.data2);
      break;
    case mk2::MidiMessageKind::kNoteOff:
      std::snprintf(line, sizeof(line), "%02d  NOTE OFF  %-3s V:%03d",
                    message.channel, MidiNoteName(message.data1).c_str(),
                    message.data2);
      break;
    case mk2::MidiMessageKind::kControlChange:
      std::snprintf(line, sizeof(line), "%02d  CC %03d    V:%03d",
                    message.channel, message.data1, message.data2);
      break;
    case mk2::MidiMessageKind::kProgramChange:
      std::snprintf(line, sizeof(line), "%02d  PC %03d", message.channel,
                    message.data1);
      break;
    case mk2::MidiMessageKind::kPitchBend:
      std::snprintf(line, sizeof(line), "%02d  PITCH     V:%05d",
                    message.channel, message.data1);
      break;
    case mk2::MidiMessageKind::kSystemExclusive:
      std::snprintf(line, sizeof(line), "--  SYSEX     %zu bytes",
                    message.raw.size());
      break;
    default:
      std::snprintf(line, sizeof(line), "--  OTHER     %zu bytes",
                    message.raw.size());
      break;
  }
  return std::string(line);
}

}  // namespace

ControllerApp::ControllerApp() {
  // Penpot AWM2 Overview default: E1 assigned, E2..E8 disabled.
  awm_elements_[0][0][0] = 1;
}

ControllerApp::~ControllerApp() { Stop(); }

bool ControllerApp::Initialize() {
  LoadControlAssignments();

  if (dry_run_) {
    std::fprintf(stderr,
                 "controller_app: --dry-run active, no writes will reach "
                 "MK2/SEQTRAK hardware\n");
  }

  hid_device_ = std::make_unique<mk2::HidDevice>();
  if (!hid_device_->OpenFirstSupported()) {
    std::fprintf(stderr, "controller_app: HID open failed: %s\n",
                 hid_device_->last_error().c_str());
    return false;
  }
  std::fprintf(stderr, "controller_app: HID opened at %s\n",
               hid_device_->path().c_str());

  lcd_device_ = std::make_unique<mk2::LcdBulkDevice>();
  if (!lcd_device_->Open()) {
    std::fprintf(stderr,
                 "controller_app: LCD bulk open failed (continuing without "
                 "display): %s\n",
                 lcd_device_->last_error().c_str());
    lcd_device_.reset();
  }

  mk2_midi_port_ = std::make_unique<mk2::AlsaRawMidiPort>();
  if (!mk2_midi_port_->OpenByNameSubstring("KOMPLETE KONTROL")) {
    std::fprintf(stderr,
                 "controller_app: MK2 MIDI port unavailable (LCD-only mode): %s\n",
                 mk2_midi_port_->last_error().c_str());
    mk2_midi_port_.reset();
  }

  seqtrak_midi_port_ = std::make_unique<mk2::AlsaRawMidiPort>();
  if (!seqtrak_midi_port_->OpenByNameSubstring("SEQTRAK")) {
    std::fprintf(stderr,
                 "controller_app: SEQTRAK MIDI port unavailable "
                 "(LCD-only mode): %s\n",
                 seqtrak_midi_port_->last_error().c_str());
    seqtrak_midi_port_.reset();
  }

  if (mk2_midi_port_) {
    router_ = std::make_unique<mk2::Mk2SeqtrakRouter>(
        mk2_midi_port_.get(), seqtrak_midi_port_.get(), dry_run_);
    router_->SetOnMk2ToSeqtrakMessage(
        [this](const mk2::MidiMessage& message) {
          OnMk2MidiMessage(message);
        });
  }

  sequencer_ = std::make_unique<mk2seq::StepSequencer>();
  sequencer_->SetNoteEventCallback(
      [this](const mk2seq::NoteEvent& event) { OnSequencerNoteEvent(event); });

  return true;
}

namespace {

// Builds the packet unconditionally (pure logic, always safe) and either
// writes it to hardware or previews it, depending on dry-run.
void SendOrPreviewLcdPacket(mk2::LcdBulkDevice* lcd_device, bool dry_run,
                             const char* screen_name,
                             const std::vector<uint8_t>& packet) {
  if (dry_run) {
    std::fprintf(stderr,
                 "[dry-run] -> LCD %s: %zu byte packet\n%s",
                 screen_name, packet.size(),
                 mk2util::PreviewHexDump(packet).c_str());
    return;
  }
  lcd_device->WritePacket(packet);
}

}  // namespace

void ControllerApp::DrawStartupScreens() {
  if (lcd_device_ == nullptr && !dry_run_) return;

  DrawLeftLcdUi();

  // The right LCD is intentionally left blank until it gets its own UI.
  mk2::LcdCanvas right;
  right.Clear(0, 0, 0);
  SendOrPreviewLcdPacket(lcd_device_.get(), dry_run_, "right",
                         mk2::BuildLcdPacket(mk2::kLcdScreenRight, right));
}

void ControllerApp::DrawLeftLcdUi() {
  if (lcd_device_ == nullptr && !dry_run_) return;

  mk2::LcdCanvas left;
  left.Clear(0, 0, 0);

  if (current_screen_ != ScreenId::kSetCcPc &&
      current_screen_ != ScreenId::kSoundSelect &&
      current_screen_ != ScreenId::kSoundList &&
      current_screen_ != ScreenId::kFmEditor &&
      current_screen_ != ScreenId::kAwm2Editor && right_lcd_has_ui_) {
    mk2::LcdCanvas blank_right;
    blank_right.Clear(0, 0, 0);
    SendOrPreviewLcdPacket(
        lcd_device_.get(), dry_run_, "right",
        mk2::BuildLcdPacket(mk2::kLcdScreenRight, blank_right));
    right_lcd_has_ui_ = false;
  }

  if (current_screen_ == ScreenId::kControllerHome) {
    DrawControllerHome(left);
    SendOrPreviewLcdPacket(lcd_device_.get(), dry_run_, "left",
                           mk2::BuildLcdPacket(mk2::kLcdScreenLeft, left));
    return;
  }
  if (current_screen_ == ScreenId::kMidiLog) {
    DrawMidiLog(left);
    SendOrPreviewLcdPacket(lcd_device_.get(), dry_run_, "left",
                           mk2::BuildLcdPacket(mk2::kLcdScreenLeft, left));
    return;
  }
  if (current_screen_ == ScreenId::kSettings) {
    DrawSettings(left);
    SendOrPreviewLcdPacket(lcd_device_.get(), dry_run_, "left",
                           mk2::BuildLcdPacket(mk2::kLcdScreenLeft, left));
    return;
  }
  if (current_screen_ == ScreenId::kSeqtrakTrackSelect) {
    DrawSeqtrakTrackSelect(left);
    SendOrPreviewLcdPacket(lcd_device_.get(), dry_run_, "left",
                           mk2::BuildLcdPacket(mk2::kLcdScreenLeft, left));
    return;
  }
  if (current_screen_ == ScreenId::kFmEditor) {
    mk2::LcdCanvas right;
    right.Clear(0, 0, 0);
    DrawFmEditor(left, false);
    DrawFmEditor(right, true);
    SendOrPreviewLcdPacket(lcd_device_.get(), dry_run_, "left",
                           mk2::BuildLcdPacket(mk2::kLcdScreenLeft, left));
    SendOrPreviewLcdPacket(lcd_device_.get(), dry_run_, "right",
                           mk2::BuildLcdPacket(mk2::kLcdScreenRight, right));
    right_lcd_has_ui_ = true;
    return;
  }
  if (current_screen_ == ScreenId::kAwm2Editor) {
    mk2::LcdCanvas right;
    right.Clear(0, 0, 0);
    DrawAwm2Editor(left, false);
    DrawAwm2Editor(right, true);
    SendOrPreviewLcdPacket(lcd_device_.get(), dry_run_, "left",
                           mk2::BuildLcdPacket(mk2::kLcdScreenLeft, left));
    SendOrPreviewLcdPacket(lcd_device_.get(), dry_run_, "right",
                           mk2::BuildLcdPacket(mk2::kLcdScreenRight, right));
    right_lcd_has_ui_ = true;
    return;
  }
  if (current_screen_ == ScreenId::kKeySplit) {
    DrawKeySplit(left);
    SendOrPreviewLcdPacket(lcd_device_.get(), dry_run_, "left",
                           mk2::BuildLcdPacket(mk2::kLcdScreenLeft, left));
    return;
  }
  if (current_screen_ == ScreenId::kSetCcPc) {
    mk2::LcdCanvas right;
    right.Clear(0, 0, 0);
    DrawSetCcPc(left, false);
    DrawSetCcPc(right, true);
    SendOrPreviewLcdPacket(lcd_device_.get(), dry_run_, "left",
                           mk2::BuildLcdPacket(mk2::kLcdScreenLeft, left));
    SendOrPreviewLcdPacket(lcd_device_.get(), dry_run_, "right",
                           mk2::BuildLcdPacket(mk2::kLcdScreenRight, right));
    right_lcd_has_ui_ = true;
    return;
  }
  if (current_screen_ == ScreenId::kSoundList) {
    mk2::LcdCanvas right;
    right.Clear(0, 0, 0);
    DrawSoundList(left, false);
    DrawSoundList(right, true);
    SendOrPreviewLcdPacket(lcd_device_.get(), dry_run_, "left",
                           mk2::BuildLcdPacket(mk2::kLcdScreenLeft, left));
    SendOrPreviewLcdPacket(lcd_device_.get(), dry_run_, "right",
                           mk2::BuildLcdPacket(mk2::kLcdScreenRight, right));
    right_lcd_has_ui_ = true;
    return;
  }
  if (current_screen_ == ScreenId::kDrumSoundCategory ||
      current_screen_ == ScreenId::kSynthSoundCategory ||
      current_screen_ == ScreenId::kDxSoundCategory ||
      current_screen_ == ScreenId::kSamplerSoundCategory ||
      current_screen_ == ScreenId::kDrumKit) {
    if (current_screen_ == ScreenId::kDrumKit) {
      DrawDrumKit(left);
    } else {
      DrawSoundDestination(left);
    }
    SendOrPreviewLcdPacket(lcd_device_.get(), dry_run_, "left",
                           mk2::BuildLcdPacket(mk2::kLcdScreenLeft, left));
    return;
  }

  // Variation 01 header. Prev. participates in jog selection while the
  // track row is active; Track Type selection keeps focus on its options.
  const bool prev_selected = lcd_ui_mode_ == LcdUiMode::kTrackSelect &&
                             selected_variation_item_ == 0;
  left.FillRect(0, 0, mk2::kLcdWidth, 28, 27, 32, 40);
  FillRoundedRect(left, 8, 4, 64, 20, 3, prev_selected ? 0 : 32,
                  prev_selected ? 215 : 42, prev_selected ? 255 : 51);
  DrawShinonomeText(left, 20, 6, "Prev.", prev_selected ? 6 : 170,
                    prev_selected ? 16 : 179, prev_selected ? 20 : 192);
  const std::string variation_title = "Variation 01";
  const int variation_title_width =
      mk2::LcdCanvas::MeasureUtf8Width(variation_title, 1);
  DrawShinonomeText(left, (mk2::kLcdWidth - variation_title_width) / 2, 6,
                    variation_title, 244, 247, 250);
  const bool variation_ok_selected = selected_variation_item_ == 12;
  FillRoundedRect(left, 420, 4, 48, 20, 3,
                  variation_ok_selected ? 0 : 32,
                  variation_ok_selected ? 215 : 42,
                  variation_ok_selected ? 255 : 51);
  DrawShinonomeText(left, 434, 6, "OK", variation_ok_selected ? 6 : 170,
                    variation_ok_selected ? 16 : 179,
                    variation_ok_selected ? 20 : 192);

  constexpr int kGap = 6;
  constexpr int kButtonHeight = 32;
  constexpr int kPaddingX = 12;
  constexpr int kRowY[] = {36, 76};
  constexpr int kRowStart[] = {0, 6};
  constexpr int kRowEnd[] = {6, 11};

  for (int row = 0; row < 2; ++row) {
    int total_width = 0;
    for (int i = kRowStart[row]; i < kRowEnd[row]; ++i) {
      total_width +=
          mk2::LcdCanvas::MeasureUtf8Width(seqtrak::kTracks[i].name, 1) +
          2 * kPaddingX;
    }
    total_width += (kRowEnd[row] - kRowStart[row] - 1) * kGap;
    int x = (mk2::kLcdWidth - total_width) / 2;
    for (int i = kRowStart[row]; i < kRowEnd[row]; ++i) {
      const std::string name = seqtrak::kTracks[i].name;
      int width = mk2::LcdCanvas::MeasureUtf8Width(name, 1) + 2 * kPaddingX;
      bool selected = selected_variation_item_ >= 1 &&
                      selected_variation_item_ <= 11 && i == selected_track_;
      FillRoundedRect(left, x, kRowY[row], width, kButtonHeight, 6,
                      selected ? 242 : 27, selected ? 184 : 32,
                      selected ? 75 : 40);
      left.DrawRect(x, kRowY[row], width, kButtonHeight,
                    selected ? 255 : 102, selected ? 215 : 112,
                    selected ? 122 : 128);
      DrawShinonomeText(left, x + kPaddingX, kRowY[row] + 8, name,
                        selected ? 17 : 244, selected ? 19 : 247,
                        selected ? 24 : 250);
      x += width + kGap;
    }
  }

  if (lcd_ui_mode_ != LcdUiMode::kTrackSelect) {
    bool has_track_type =
        selected_track_ != 9 && selected_track_ != 10;  // DX / SAMPLER
    if (has_track_type) {
      DrawShinonomeText(left, 12, 105, "TrackType", 244, 247, 250);
      constexpr int kOptionX[] = {118, 222, 350};
      for (int i = 0; i < 3; ++i) {
        bool selected = track_types_[selected_track_] == i;
        left.DrawRect(kOptionX[i], 105, 16, 16, selected ? 242 : 122,
                      selected ? 184 : 132, selected ? 75 : 146);
        if (selected) {
          left.FillRect(kOptionX[i] + 5, 110, 6, 6, 242, 184, 75);
        }
        DrawShinonomeText(left, kOptionX[i] + 22, 105, kTrackTypeNames[i],
                          selected ? 242 : 244, selected ? 184 : 247,
                          selected ? 75 : 250);
      }
    }

    const std::string category = track_sound_categories_[selected_track_].empty()
                                     ? "--"
                                     : track_sound_categories_[selected_track_];
    const std::string sound = track_sound_names_[selected_track_].empty()
                                  ? "--"
                                  : track_sound_names_[selected_track_];
    DrawShinonomeText(left, 12, 160, "Category: " + category, 79, 195, 247);
    DrawShinonomeText(left, 12, 194, "Sound: " + sound, 244, 247, 250);
  }
  if (!variation_status_.empty()) {
    DrawShinonomeText(left, 12, 240, variation_status_, 72, 213, 151);
  }

  SendOrPreviewLcdPacket(lcd_device_.get(), dry_run_, "left",
                         mk2::BuildLcdPacket(mk2::kLcdScreenLeft, left));
  mk2::LcdCanvas right;
  right.Clear(0, 0, 0);
  DrawVariationSummary(right);
  SendOrPreviewLcdPacket(lcd_device_.get(), dry_run_, "right",
                         mk2::BuildLcdPacket(mk2::kLcdScreenRight, right));
  right_lcd_has_ui_ = true;
}

void ControllerApp::DrawSoundDestination(mk2::LcdCanvas& canvas) {
  const char* title = "Sound Category";
  const std::array<const char*, 15>* categories = &kDrumCategories;
  switch (current_screen_) {
    case ScreenId::kDrumSoundCategory:
      title = "Drum Sound Category";
      categories = &kDrumCategories;
      break;
    case ScreenId::kSynthSoundCategory:
      title = "Synth Sound Category";
      categories = &kSynthCategories;
      break;
    case ScreenId::kDxSoundCategory:
      title = "DX Sound Category";
      categories = &kDxCategories;
      break;
    case ScreenId::kSamplerSoundCategory:
      title = "SAMPLER Sound Category";
      categories = &kSamplerCategories;
      break;
    default:
      break;
  }

  canvas.FillRect(0, 0, mk2::kLcdWidth, 28, 27, 32, 40);
  FillRoundedRect(canvas, 8, 4, 64, 20, 3, 0, 215, 255);
  DrawShinonomeText(canvas, 20, 6, "Prev.", 6, 16, 20);
  const int title_width = mk2::LcdCanvas::MeasureUtf8Width(title, 1);
  DrawShinonomeText(canvas, (mk2::kLcdWidth - title_width) / 2, 6, title,
                    244, 247, 250);
  constexpr int kButtonWidth = 148;
  constexpr int kButtonHeight = 39;
  constexpr int kGapX = 8;
  constexpr int kGapY = 4;
  for (int index = 0; index < 15; ++index) {
    const int row = index / 3;
    const int column = index % 3;
    const int x = 10 + column * (kButtonWidth + kGapX);
    const int y = 34 + row * (kButtonHeight + kGapY);
    const bool selected = selected_sound_category_item_ == index + 1;
    FillRoundedRect(canvas, x, y, kButtonWidth, kButtonHeight, 3,
                    selected ? 0 : 32, selected ? 215 : 42,
                    selected ? 255 : 51);
    canvas.DrawRect(x, y, kButtonWidth, kButtonHeight,
                    selected ? 184 : 82, selected ? 245 : 97,
                    selected ? 255 : 109);

    const auto lines = WrapCategoryLabel((*categories)[index]);
    const int first_y = y + (kButtonHeight - static_cast<int>(lines.size()) *
                                                12) /
                                2;
    for (size_t line = 0; line < lines.size(); ++line) {
      const int line_width =
          mk2::LcdCanvas::MeasureUtf8Width(lines[line], 1);
      DrawShinonomeText(canvas, x + (kButtonWidth - line_width) / 2,
                        first_y + static_cast<int>(line) * 12, lines[line],
                        selected ? 6 : 242, selected ? 16 : 247,
                        selected ? 20 : 250);
    }
  }
}

void ControllerApp::DrawDrumKit(mk2::LcdCanvas& canvas) {
  canvas.FillRect(0, 0, mk2::kLcdWidth, 28, 27, 32, 40);
  const bool prev_selected = selected_drum_kit_item_ == 0;
  FillRoundedRect(canvas, 8, 4, 64, 20, 3, prev_selected ? 0 : 32,
                  prev_selected ? 215 : 42, prev_selected ? 255 : 51);
  DrawShinonomeText(canvas, 20, 6, "Prev.", prev_selected ? 6 : 170,
                    prev_selected ? 16 : 179, prev_selected ? 20 : 192);
  const std::string title = "Drum Kit";
  const int title_width = mk2::LcdCanvas::MeasureUtf8Width(title, 1);
  DrawShinonomeText(canvas, (mk2::kLcdWidth - title_width) / 2, 6, title,
                    244, 247, 250);
  const bool ok_selected = selected_drum_kit_item_ == 9;
  FillRoundedRect(canvas, 420, 4, 48, 20, 3, ok_selected ? 0 : 32,
                  ok_selected ? 215 : 42, ok_selected ? 255 : 51);
  DrawShinonomeText(canvas, 434, 6, "OK", ok_selected ? 6 : 170,
                    ok_selected ? 16 : 179, ok_selected ? 20 : 192);

  constexpr const char* kLabels[] = {
      "Type", "KICK", "SNARE", "CLAP", "HAT 1", "HAT 2", "PERC 1",
      "PERC 2"};
  constexpr int kButtonX[] = {12, 126, 240, 354, 12, 126, 240, 354};
  constexpr int kButtonY[] = {48, 48, 48, 48, 112, 112, 112, 112};
  constexpr int kButtonWidth = 102;
  constexpr int kButtonHeight = 48;
  for (int i = 0; i < 8; ++i) {
    const bool selected = selected_drum_kit_item_ == i + 1;
    FillRoundedRect(canvas, kButtonX[i], kButtonY[i], kButtonWidth,
                    kButtonHeight, 4, selected ? 0 : 32,
                    selected ? 215 : 42, selected ? 255 : 51);
    canvas.DrawRect(kButtonX[i], kButtonY[i], kButtonWidth, kButtonHeight,
                    selected ? 184 : 82, selected ? 245 : 97,
                    selected ? 255 : 109);
    const int label_width = mk2::LcdCanvas::MeasureUtf8Width(kLabels[i], 1);
    const int label_y = i == 0 ? kButtonY[i] + 17 : kButtonY[i] + 5;
    DrawShinonomeText(canvas,
                      kButtonX[i] + (kButtonWidth - label_width) / 2,
                      label_y, kLabels[i], selected ? 6 : 242,
                      selected ? 16 : 247, selected ? 20 : 250);
    if (i > 0) {
      const std::string sound = drum_kit_sound_names_[i - 1].empty()
                                    ? "--"
                                    : drum_kit_sound_names_[i - 1];
      const std::string short_sound = Ellipsize(sound, kButtonWidth - 8);
      const int sound_width =
          mk2::LcdCanvas::MeasureUtf8Width(short_sound, 1);
      DrawShinonomeText(canvas,
                        kButtonX[i] + (kButtonWidth - sound_width) / 2,
                        kButtonY[i] + 25, short_sound,
                        selected ? 6 : 170, selected ? 16 : 179,
                        selected ? 20 : 192);
    }
  }

  canvas.DrawRect(12, 180, 444, 68, 82, 97, 109);
  const int part_index = std::clamp(selected_drum_kit_item_ - 2, 0, 6);
  std::string selected_part = "Select Type or Part";
  if (selected_drum_kit_item_ == 1) {
    selected_part = "Type: Drum Kit";
  } else if (selected_drum_kit_item_ >= 2 && selected_drum_kit_item_ <= 8) {
    selected_part = "Part: " + std::string(kLabels[part_index + 1]);
  } else if (selected_drum_kit_item_ == 9) {
    selected_part = "Confirm Drum Kit";
  }
  DrawShinonomeText(canvas, 24, 194, selected_part, 244, 247, 250);
  DrawShinonomeText(canvas, 24, 222, "Vol:100", 79, 195, 247);
  DrawShinonomeText(canvas, 180, 222, "Pan:0", 79, 195, 247);
}

void ControllerApp::DrawVariationSummary(mk2::LcdCanvas& canvas) {
  canvas.FillRect(0, 0, mk2::kLcdWidth, 28, 27, 32, 40);
  DrawShinonomeText(canvas, 12, 6, "TRACK", 170, 179, 192);
  DrawShinonomeText(canvas, 92, 6, "CATEGORY", 170, 179, 192);
  DrawShinonomeText(canvas, 244, 6, "SOUND", 170, 179, 192);
  for (int track = 0; track < 11; ++track) {
    const int y = 34 + track * 21;
    const bool selected = selected_variation_item_ >= 1 &&
                          selected_variation_item_ <= 11 &&
                          selected_track_ == track;
    if (selected) canvas.FillRect(6, y - 2, 468, 19, 37, 73, 88);
    const std::string category = track_sound_categories_[track].empty()
                                     ? "--"
                                     : track_sound_categories_[track];
    const std::string sound =
        track_sound_names_[track].empty() ? "--" : track_sound_names_[track];
    DrawShinonomeText(canvas, 12, y, seqtrak::kTracks[track].name,
                      selected ? 234 : 244, selected ? 248 : 247,
                      selected ? 255 : 250);
    DrawShinonomeText(canvas, 92, y, Ellipsize(category, 140), 79, 195, 247);
    DrawShinonomeText(canvas, 244, y, Ellipsize(sound, 224), 244, 247, 250);
  }
}

void ControllerApp::DrawSoundList(mk2::LcdCanvas& canvas,
                                  bool right_screen) {
  const auto sounds = FilteredSoundPresets(selected_sound_kind_,
                                           selected_sound_category_index_);
  const int selected = std::max(selected_sound_list_item_, 0);
  const int page = selected / 20;
  const int base = page * 20 + (right_screen ? 10 : 0);

  canvas.FillRect(0, 0, mk2::kLcdWidth, 28, 27, 32, 40);
  if (!right_screen) {
    const bool prev_selected = selected_sound_list_item_ == -1;
    FillRoundedRect(canvas, 8, 4, 64, 20, 3, prev_selected ? 0 : 32,
                    prev_selected ? 215 : 42, prev_selected ? 255 : 51);
    DrawShinonomeText(canvas, 20, 6, "Prev.", prev_selected ? 6 : 170,
                      prev_selected ? 16 : 179, prev_selected ? 20 : 192);
    DrawShinonomeText(canvas, 92, 6, "Sound List", 244, 247, 250);
  } else {
    const std::string category = CategoryNameForKind(
        selected_sound_kind_, selected_sound_category_index_);
    DrawShinonomeText(canvas, 12, 6, Ellipsize(category, 340), 244, 247, 250);
  }
  const std::string page_text = std::to_string(page + 1) + "/" +
                                std::to_string(std::max(
                                    1, (static_cast<int>(sounds.size()) + 19) /
                                           20));
  DrawShinonomeText(canvas, 414, 6, page_text, 170, 179, 192);

  for (int row = 0; row < 10; ++row) {
    const int index = base + row;
    if (index >= static_cast<int>(sounds.size())) break;
    const int y = 38 + row * 22;
    const bool row_selected = selected_sound_list_item_ == index;
    if (row_selected) canvas.FillRect(8, y - 2, 456, 19, 37, 73, 88);
    char number[8];
    std::snprintf(number, sizeof(number), "%04d", sounds[index]->number);
    DrawShinonomeText(canvas, 14, y, number, 170, 179, 192);
    DrawShinonomeText(canvas, 66, y, Ellipsize(sounds[index]->name, 390),
                      row_selected ? 234 : 244,
                      row_selected ? 248 : 247,
                      row_selected ? 255 : 250);
  }
}

void ControllerApp::DrawControllerHome(mk2::LcdCanvas& canvas) {
  const auto draw_centered = [&canvas](int y, const std::string& text,
                                       uint8_t r, uint8_t g, uint8_t b) {
    const int width = mk2::LcdCanvas::MeasureUtf8Width(text, 1);
    DrawShinonomeText(canvas, (mk2::kLcdWidth - width) / 2, y, text, r, g, b);
  };

  draw_centered(36, "SEQTRAK controller", 244, 247, 250);
  draw_centered(68, "Using KOMPLETE KONTROL S49 MK2", 184, 197, 206);

  const bool s49_connected =
      hid_device_ != nullptr && hid_device_->IsOpen() &&
      mk2_midi_port_ != nullptr;
  const bool seqtrak_connected = seqtrak_midi_port_ != nullptr;
  std::string status = "Status  S49:";
  status += s49_connected ? "OK" : "--";
  status += "  SEQTRAK:";
  status += seqtrak_connected ? "OK" : "--";
  draw_centered(104, status, s49_connected && seqtrak_connected ? 72 : 242,
                s49_connected && seqtrak_connected ? 213 : 184,
                s49_connected && seqtrak_connected ? 151 : 75);

  constexpr int kButtonY = 144;
  constexpr int kButtonHeight = 66;
  for (int i = 0; i < 3; ++i) {
    const bool selected = selected_home_button_ == i;
    const auto& spec = kHomeButtons[i];
    FillRoundedRect(canvas, spec.x, kButtonY, spec.width, kButtonHeight, 4,
                    selected ? 0 : 32, selected ? 215 : 42,
                    selected ? 255 : 51);
    canvas.DrawRect(spec.x, kButtonY, spec.width, kButtonHeight,
                    selected ? 184 : 82, selected ? 245 : 97,
                    selected ? 255 : 109);
    const int label_width =
        mk2::LcdCanvas::MeasureUtf8Width(spec.label, 1);
    DrawShinonomeText(canvas, spec.x + (spec.width - label_width) / 2,
                      kButtonY + 25, spec.label, selected ? 6 : 242,
                      selected ? 16 : 247, selected ? 20 : 249);
  }

  const bool playing =
      router_ != nullptr && router_->IsMk2ToSeqtrakForwardingEnabled();
  draw_centered(232, playing ? "MIDI thru: ON" : "MIDI thru: OFF",
                playing ? 72 : 130, playing ? 213 : 147,
                playing ? 151 : 160);
}

void ControllerApp::DrawMidiLog(mk2::LcdCanvas& canvas) {
  constexpr int kHeaderHeight = 28;
  canvas.FillRect(0, 0, mk2::kLcdWidth, kHeaderHeight, 27, 32, 40);
  canvas.DrawRect(0, 0, mk2::kLcdWidth, kHeaderHeight, 82, 97, 109);

  FillRoundedRect(canvas, 8, 4, 64, 20, 3, 0, 215, 255);
  DrawShinonomeText(canvas, 20, 6, "Prev.", 6, 16, 20);

  const std::string title = "MIDI LOG";
  const int title_width = mk2::LcdCanvas::MeasureUtf8Width(title, 1);
  DrawShinonomeText(canvas, (mk2::kLcdWidth - title_width) / 2, 6, title,
                    244, 247, 250);

  DrawShinonomeText(canvas, 16, 42, "CH", 170, 179, 192);
  DrawShinonomeText(canvas, 72, 42, "TYPE", 170, 179, 192);
  DrawShinonomeText(canvas, 192, 42, "DATA", 170, 179, 192);
  canvas.DrawRect(8, 34, 448, 218, 82, 97, 109);
  canvas.FillRect(456, 34, 8, 218, 37, 43, 52);
  FillRoundedRect(canvas, 457, 36, 6, 54, 3, 130, 147, 160);

  std::vector<std::string> lines;
  {
    std::lock_guard<std::mutex> lock(midi_log_mutex_);
    lines.assign(midi_log_lines_.begin(), midi_log_lines_.end());
  }
  if (lines.empty()) {
    DrawShinonomeText(canvas, 16, 76, "Waiting for MIDI messages...",
                      130, 147, 160);
    return;
  }
  for (size_t i = 0; i < lines.size(); ++i) {
    DrawShinonomeText(canvas, 16, 66 + static_cast<int>(i) * 16, lines[i],
                      244, 247, 250);
  }
}

void ControllerApp::DrawSettings(mk2::LcdCanvas& canvas) {
  constexpr int kHeaderHeight = 28;
  canvas.FillRect(0, 0, mk2::kLcdWidth, kHeaderHeight, 27, 32, 40);
  canvas.DrawRect(0, 0, mk2::kLcdWidth, kHeaderHeight, 82, 97, 109);

  const bool prev_selected = selected_settings_item_ == 0;
  FillRoundedRect(canvas, 8, 4, 64, 20, 3, prev_selected ? 0 : 32,
                  prev_selected ? 215 : 42, prev_selected ? 255 : 51);
  DrawShinonomeText(canvas, 20, 6, "Prev.", prev_selected ? 6 : 170,
                    prev_selected ? 16 : 179, prev_selected ? 20 : 192);

  const std::string title = "Settings";
  const int title_width = mk2::LcdCanvas::MeasureUtf8Width(title, 1);
  DrawShinonomeText(canvas, (mk2::kLcdWidth - title_width) / 2, 6, title,
                    244, 247, 250);

  constexpr const char* kLabels[] = {
      "S49MK2", "Key Split", "Set CC", "SEQTRAK", "Controller"};
  constexpr int kButtonX[] = {12, 168, 324, 90, 246};
  constexpr int kButtonY[] = {50, 50, 50, 136, 136};
  constexpr int kButtonWidth = 144;
  constexpr int kButtonHeight = 66;
  for (size_t i = 0; i < std::size(kLabels); ++i) {
    const bool selected = selected_settings_item_ == static_cast<int>(i) + 1;
    FillRoundedRect(canvas, kButtonX[i], kButtonY[i], kButtonWidth,
                    kButtonHeight, 4, selected ? 0 : 32,
                    selected ? 215 : 42, selected ? 255 : 51);
    canvas.DrawRect(kButtonX[i], kButtonY[i], kButtonWidth, kButtonHeight,
                    selected ? 184 : 82, selected ? 245 : 97,
                    selected ? 255 : 109);
    const int label_width =
        mk2::LcdCanvas::MeasureUtf8Width(kLabels[i], 1);
    DrawShinonomeText(canvas,
                      kButtonX[i] + (kButtonWidth - label_width) / 2,
                      kButtonY[i] + 25, kLabels[i], selected ? 6 : 242,
                      selected ? 16 : 247, selected ? 20 : 249);
  }
}

void ControllerApp::DrawSeqtrakTrackSelect(mk2::LcdCanvas& canvas) {
  canvas.FillRect(0, 0, mk2::kLcdWidth, 28, 27, 32, 40);
  const bool prev = selected_seqtrak_track_ < 0;
  FillRoundedRect(canvas, 8, 4, 64, 20, 3, prev ? 0 : 32,
                  prev ? 215 : 42, prev ? 255 : 51);
  DrawShinonomeText(canvas, 20, 6, "Prev.", prev ? 6 : 170,
                    prev ? 16 : 179, prev ? 20 : 192);
  const std::string title = "TRACK SELECT";
  DrawShinonomeText(canvas,
                    (mk2::kLcdWidth - mk2::LcdCanvas::MeasureUtf8Width(title, 1)) / 2,
                    6, title, 244, 247, 250);
  for (int i = 0; i < static_cast<int>(seqtrak::kTrackCount); ++i) {
    const int row = i / 6;
    const int col = i % 6;
    const int x = 8 + col * 78;
    const int y = 48 + row * 74;
    const bool selected = selected_seqtrak_track_ == i;
    FillRoundedRect(canvas, x, y, 70, 54, 4, selected ? 0 : 32,
                    selected ? 215 : 42, selected ? 255 : 51);
    DrawShinonomeText(canvas, x + 8, y + 20, seqtrak::kTracks[i].name,
                      selected ? 6 : 242, selected ? 16 : 247,
                      selected ? 20 : 249);
  }
  DrawShinonomeText(canvas, 12, 216,
                    selected_seqtrak_track_ == 9
                        ? "Press: open FM Editor"
                        : "Select a SEQTRAK track",
                    130, 147, 160);
}

void ControllerApp::DrawFmEditor(mk2::LcdCanvas& canvas, bool right_screen) {
  const auto& page = kFmPages[fm_page_];
  canvas.FillRect(0, 0, mk2::kLcdWidth, 28, 27, 32, 40);
  if (!right_screen) {
    const bool prev = selected_fm_header_action_ == 0;
    FillRoundedRect(canvas, 8, 4, 72, 20, 3, prev ? 0 : 32,
                    prev ? 215 : 42, prev ? 255 : 51);
    DrawShinonomeText(canvas, 14, 6, "<PREV", prev ? 6 : 170,
                      prev ? 16 : 179, prev ? 20 : 192);
    if (fm_page_ == 0) {
      const bool save = selected_fm_header_action_ == 1;
      FillRoundedRect(canvas, 400, 4, 68, 20, 3, save ? 0 : 32,
                      save ? 215 : 42, save ? 255 : 51);
      DrawShinonomeText(canvas, 412, 6, "Save", save ? 6 : 170,
                        save ? 16 : 179, save ? 20 : 192);
    }
    DrawShinonomeText(canvas, 104, 6, page.title, 244, 247, 250);
  } else {
    DrawShinonomeText(canvas, 8, 6, "DX:FM EDITOR", 244, 247, 250);
    DrawShinonomeText(canvas, 370, 6,
                      std::to_string(fm_page_ + 1) + "/15", 170, 179, 192);
  }

  // L01 has its own Penpot layout: an FM routing diagram on the left LCD,
  // not the generic four-parameter columns used by the remaining pages.
  if (fm_page_ == 0 && !right_screen) {
    const int algorithm = fm_common_[3] % 12;
    char algorithm_text[24];
    std::snprintf(algorithm_text, sizeof(algorithm_text), "ALGORITHM  %02d/12",
                  algorithm + 1);
    const int title_width =
        mk2::LcdCanvas::MeasureUtf8Width(algorithm_text, 1);
    DrawShinonomeText(canvas, (mk2::kLcdWidth - title_width) / 2, 40,
                      algorithm_text, 255, 145, 45);

    // Each entry says where OP1..OP4 feeds: -1=OUTPUT, 0..3=another OP.
    // The twelve routings deliberately use the same four fixed operator
    // positions as the Penpot board while changing the connecting graph.
    constexpr int kRoutes[12][4] = {
        {-1, 0, 1, 2}, {-1, -1, 0, 1}, {-1, 0, -1, 2},
        {-1, 0, 0, 2}, {-1, -1, 1, 2}, {-1, 0, -1, 1},
        {-1, -1, 0, 2}, {-1, -1, -1, 0}, {-1, 0, 1, -1},
        {-1, -1, 1, -1}, {-1, 0, -1, -1}, {-1, -1, -1, -1}};
    constexpr int kOpX[4] = {304, 64, 304, 64};
    constexpr int kOpY[4] = {132, 132, 68, 68};
    constexpr int kBoxW = 112;
    constexpr int kBoxH = 40;
    constexpr int kOutputY = 208;

    // Connections go behind operator boxes. Elbows keep every route legible
    // in the LCD's compact 480x272 coordinate space.
    for (int source = 0; source < 4; ++source) {
      const int sx = kOpX[source] + kBoxW / 2;
      const int sy = kOpY[source] + kBoxH;
      const int target = kRoutes[algorithm][source];
      if (target < 0) {
        canvas.FillRect(sx, sy, 2, kOutputY - sy, 255, 145, 45);
      } else {
        const int tx = kOpX[target] + kBoxW / 2;
        const int ty = kOpY[target];
        const int elbow_y = sy + std::max(2, (ty - sy) / 2);
        canvas.FillRect(sx, sy, 2, std::max(2, elbow_y - sy), 255, 145, 45);
        canvas.FillRect(std::min(sx, tx), elbow_y,
                        std::max(2, std::abs(tx - sx) + 2), 2,
                        255, 145, 45);
        canvas.FillRect(tx, elbow_y, 2, std::max(2, ty - elbow_y),
                        255, 145, 45);
      }
    }

    for (int op = 0; op < 4; ++op) {
      canvas.FillRect(kOpX[op], kOpY[op], kBoxW, kBoxH, 27, 32, 40);
      canvas.DrawRect(kOpX[op], kOpY[op], kBoxW, kBoxH,
                      op < 2 ? 255 : 130, op < 2 ? 145 : 147,
                      op < 2 ? 45 : 160);
      const std::string label = "OP" + std::to_string(op + 1);
      DrawShinonomeText(
          canvas,
          kOpX[op] +
              (kBoxW - mk2::LcdCanvas::MeasureUtf8Width(label, 1)) / 2,
          kOpY[op] + 13, label, op < 2 ? 255 : 244,
          op < 2 ? 145 : 247, op < 2 ? 45 : 250);
    }
    FillRoundedRect(canvas, 148, 58, 30, 18, 2, 38, 208, 161);
    DrawShinonomeText(canvas, 154, 60, "FB", 6, 16, 20);
    canvas.DrawRect(40, kOutputY, 400, 16, 255, 145, 45);
    DrawShinonomeText(canvas, 214, kOutputY + 2, "OUTPUT", 255, 145, 45);

    constexpr const char* kMiniLabels[] = {"FEEDBACK", "LFO SPD", "LFO PMD"};
    constexpr int kMiniIndex[] = {0, 1, 2};
    constexpr int kMiniX[] = {0, 160, 320};
    for (int i = 0; i < 3; ++i) {
      if (i > 0) canvas.FillRect(kMiniX[i], 232, 1, 40, 55, 65, 81);
      DrawShinonomeText(canvas, kMiniX[i] + 12, 234, kMiniLabels[i],
                        170, 179, 192);
      DrawShinonomeText(canvas, kMiniX[i] + 68, 252,
                        std::to_string(fm_common_[kMiniIndex[i]]),
                        255, 145, 45);
    }
    if (!fm_status_.empty())
      DrawShinonomeText(canvas, 260, 252, fm_status_, 72, 213, 151);
    return;
  }

  const bool is_operator_eg =
      fm_page_ == 2 || fm_page_ == 5 || fm_page_ == 8 || fm_page_ == 11;
  const bool is_common_peg = fm_page_ == 14;
  if (is_operator_eg || is_common_peg) {
    std::array<int, 8> envelope{};
    if (is_operator_eg) {
      const int op = (fm_page_ - 2) / 3;
      for (int i = 0; i < 8; ++i)
        envelope[i] = fm_operators_[op][8 + i];
    } else {
      for (int i = 0; i < 8; ++i) envelope[i] = fm_common_[16 + i];
    }

    constexpr int kGraphLeft = 20;
    constexpr int kGraphRight = 460;
    constexpr int kGraphTop = 42;
    constexpr int kGraphBottom = 166;
    canvas.DrawRect(kGraphLeft, kGraphTop, kGraphRight - kGraphLeft,
                    kGraphBottom - kGraphTop, 82, 97, 109);
    for (int grid = 1; grid < 4; ++grid) {
      const int y = kGraphTop + grid * (kGraphBottom - kGraphTop) / 4;
      for (int x = kGraphLeft + 2; x < kGraphRight - 2; x += 8)
        canvas.FillRect(x, y, 3, 1, 55, 65, 81);
    }

    // A larger RATE reaches the next level faster. Normalize all four
    // segments so the complete envelope always fits the graph width.
    std::array<int, 4> duration{};
    int duration_sum = 0;
    for (int i = 0; i < 4; ++i) {
      duration[i] = std::max(8, 135 - envelope[i]);
      duration_sum += duration[i];
    }
    std::array<int, 5> px{};
    std::array<int, 5> py{};
    px[0] = kGraphLeft + 2;
    py[0] = kGraphBottom - 2;
    int accumulated = 0;
    for (int point = 1; point <= 4; ++point) {
      accumulated += duration[point - 1];
      px[point] = kGraphLeft + 2 +
                  accumulated * (kGraphRight - kGraphLeft - 4) /
                      duration_sum;
      py[point] = kGraphBottom - 2 -
                  envelope[3 + point] * (kGraphBottom - kGraphTop - 4) / 127;
      DrawLine(canvas, px[point - 1], py[point - 1], px[point], py[point],
               0, 215, 255);
      canvas.FillRect(px[point] - 3, py[point] - 3, 7, 7, 255, 145, 45);
    }

    const int first_parameter = right_screen ? 4 : 0;
    for (int col = 0; col < 4; ++col) {
      const int parameter = first_parameter + col;
      const int x = col * 120;
      if (col > 0) canvas.FillRect(x, 178, 1, 86, 55, 65, 81);
      DrawShinonomeText(canvas, x + 10, 182, page.labels[parameter],
                        170, 179, 192);
      const std::string value = std::to_string(envelope[parameter]);
      DrawShinonomeText(
          canvas,
          x + (120 - mk2::LcdCanvas::MeasureUtf8Width(value, 1)) / 2,
          214, value, 244, 247, 250);
      DrawShinonomeText(canvas, x + 38, 242, "0-127", 130, 147, 160);
    }
    if (!fm_status_.empty())
      DrawShinonomeText(canvas, 300, 246, fm_status_, 72, 213, 151);
    return;
  }

  for (int col = 0; col < 4; ++col) {
    const int parameter = (right_screen ? 4 : 0) + col;
    const char* label = page.labels[parameter];
    int value = 0;
    if (fm_page_ == 0) value = fm_common_[parameter];
    else if (fm_page_ <= 12) {
      const int op = (fm_page_ - 1) / 3;
      const int group = (fm_page_ - 1) % 3;
      value = fm_operators_[op][group * 8 + parameter];
    } else {
      value = fm_common_[(fm_page_ == 13 ? 8 : 16) + parameter];
    }
    const int x = col * 120;
    if (col > 0) canvas.FillRect(x, 32, 1, 232, 55, 65, 81);
    DrawShinonomeText(canvas, x + 10, 48, label, 170, 179, 192);
    canvas.FillRect(x + 14, 92, 92, 12, 37, 43, 52);
    canvas.FillRect(x + 14, 92, value * 92 / 127, 12, 0, 215, 255);
    DrawShinonomeText(canvas, x + 48, 132, std::to_string(value),
                      244, 247, 250);
    DrawShinonomeText(canvas, x + 38, 190, "0-127", 130, 147, 160);
  }
  if (!fm_status_.empty())
    DrawShinonomeText(canvas, 12, 246, fm_status_, 72, 213, 151);
}

void ControllerApp::DrawAwm2Editor(mk2::LcdCanvas& canvas,
                                   bool right_screen) {
  const auto page = GetAwmPageSpec(awm_page_);
  canvas.FillRect(0, 0, mk2::kLcdWidth, 28, 27, 32, 40);
  if (!right_screen) {
    FillRoundedRect(canvas, 8, 4, 72, 20, 3, 32, 42, 51);
    DrawShinonomeText(canvas, 14, 6, "<PREV", 170, 179, 192);
    DrawShinonomeText(canvas, 104, 6, page.title, 244, 247, 250);
  } else {
    DrawShinonomeText(canvas, 8, 6, seqtrak::kTracks[awm_track_].name,
                      244, 247, 250);
    DrawShinonomeText(canvas, 370, 6,
                      std::to_string(awm_page_ + 1) + "/39", 170, 179, 192);
  }

  if (awm_page_ == 0 && awm_overview_mode_ == AwmOverviewMode::kCategory) {
    const int begin = right_screen ? 8 : 0;
    const int end = right_screen ? 15 : 8;
    for (int index = begin; index < end; ++index) {
      const int local = index - begin;
      const int y = 42 + local * 28;
      const bool selected = index == selected_awm_category_;
      if (selected) canvas.FillRect(12, y - 2, 452, 24, 37, 73, 88);
      DrawShinonomeText(canvas, 24, y, kSynthCategories[index],
                        selected ? 79 : 244, selected ? 195 : 247,
                        selected ? 247 : 250);
    }
    return;
  }
  if (awm_page_ == 0 && awm_overview_mode_ == AwmOverviewMode::kSound) {
    const auto sounds = FilteredSoundPresets(1, selected_awm_category_);
    const int page_start = (selected_awm_sound_ / 16) * 16;
    const int begin = page_start + (right_screen ? 8 : 0);
    const int end = std::min(begin + 8, static_cast<int>(sounds.size()));
    for (int index = begin; index < end; ++index) {
      const int y = 42 + (index - begin) * 28;
      const bool selected = index == selected_awm_sound_;
      if (selected) canvas.FillRect(12, y - 2, 452, 24, 37, 73, 88);
      DrawShinonomeText(canvas, 24, y,
                        Ellipsize(sounds[index]->name, 420),
                        selected ? 79 : 244, selected ? 195 : 247,
                        selected ? 247 : 250);
    }
    return;
  }
  if (awm_page_ == 0 && !right_screen) {
    for (int element = 0; element < 8; ++element) {
      const int y = 36 + element * 28;
      const auto& basic = awm_elements_[element][0];
      const bool assigned = basic[0] != 0;
      if (element == selected_awm_element_)
        canvas.FillRect(16, y - 2, 448, 24, 37, 73, 88);
      DrawShinonomeText(canvas, 32, y,
                        "E" + std::to_string(element + 1),
                        assigned ? 255 : 130, assigned ? 145 : 147,
                        assigned ? 45 : 160);
      DrawShinonomeText(canvas, 80, y,
                        assigned ? (awm_element_sound_names_[element].empty()
                                        ? "SELECT SOUND"
                                        : awm_element_sound_names_[element])
                                 : "OFF",
                        244, 247, 250);
      DrawShinonomeText(canvas, 400, y, assigned ? "ON" : "OFF",
                        170, 179, 192);
    }
    return;
  }
  if (awm_page_ == 0 && right_screen) {
    const bool assigned = awm_elements_[selected_awm_element_][0][0] != 0;
    DrawShinonomeText(canvas, 24, 48,
                      "ELEMENT E" + std::to_string(selected_awm_element_ + 1),
                      244, 247, 250);
    DrawShinonomeText(canvas, 24, 88,
                      std::string("STATUS: ") + (assigned ? "ON" : "OFF"),
                      assigned ? 72 : 170, assigned ? 213 : 179,
                      assigned ? 151 : 192);
    if (assigned) {
      DrawShinonomeText(canvas, 24, 132,
                        "CATEGORY: " +
                            std::string(kSynthCategories[awm_element_categories_[selected_awm_element_]]),
                        79, 195, 247);
      DrawShinonomeText(canvas, 24, 172,
                        "SOUND: " +
                            (awm_element_sound_names_[selected_awm_element_].empty()
                                 ? std::string("--")
                                 : awm_element_sound_names_[selected_awm_element_]),
                        244, 247, 250);
    }
    DrawShinonomeText(canvas, 24, 228, "Press: ON/OFF", 130, 147, 160);
    return;
  }

  std::array<uint8_t, 8>* values = nullptr;
  if (awm_page_ < 5) values = &awm_common_[awm_page_];
  else if (awm_page_ < 37) {
    const int element = (awm_page_ - 5) / 4;
    const int section = (awm_page_ - 5) % 4;
    values = &awm_elements_[element][section];
  } else {
    values = &awm_insertions_[awm_page_ - 37];
  }

  const bool aeg = awm_page_ >= 7 && awm_page_ < 37 &&
                   (awm_page_ - 5) % 4 == 2;
  if (aeg) {
    constexpr int left = 20, right = 460, top = 42, bottom = 166;
    canvas.DrawRect(left, top, right - left, bottom - top, 82, 97, 109);
    std::array<int, 5> x = {left + 2, 0, 0, 0, right - 2};
    std::array<int, 5> y = {bottom - 2, 0, 0, 0, 0};
    int sum = 0;
    std::array<int, 4> duration{};
    for (int i = 0; i < 4; ++i) {
      duration[i] = std::max(8, 135 - static_cast<int>((*values)[i]));
      sum += duration[i];
    }
    int elapsed = 0;
    for (int i = 1; i <= 4; ++i) {
      elapsed += duration[i - 1];
      x[i] = left + 2 + elapsed * (right - left - 4) / sum;
      y[i] = bottom - 2 - (*values)[i + 3] * (bottom - top - 4) / 127;
      DrawLine(canvas, x[i - 1], y[i - 1], x[i], y[i], 0, 215, 255);
      canvas.FillRect(x[i] - 3, y[i] - 3, 7, 7, 255, 145, 45);
    }
  }

  const int base = right_screen ? 4 : 0;
  for (int col = 0; col < 4; ++col) {
    const int parameter = base + col;
    const int x = col * 120;
    const int label_y = aeg ? 182 : 48;
    if (col > 0) canvas.FillRect(x, aeg ? 178 : 32, 1, aeg ? 86 : 232,
                                 55, 65, 81);
    DrawShinonomeText(canvas, x + 10, label_y, page.labels[parameter],
                      170, 179, 192);
    if (!aeg) {
      canvas.FillRect(x + 14, 92, 92, 12, 37, 43, 52);
      canvas.FillRect(x + 14, 92, (*values)[parameter] * 92 / 127, 12,
                      0, 215, 255);
    }
    DrawShinonomeText(canvas, x + 48, aeg ? 214 : 132,
                      std::to_string((*values)[parameter]), 244, 247, 250);
    DrawShinonomeText(canvas, x + 38, aeg ? 242 : 190, "0-127",
                      130, 147, 160);
  }
}

void ControllerApp::DrawKeySplit(mk2::LcdCanvas& canvas) {
  canvas.FillRect(0, 0, mk2::kLcdWidth, 28, 27, 32, 40);
  DrawShinonomeText(canvas, 12, 6, "Key Split", 244, 247, 250);
  constexpr const char* kActions[] = {"OK", "Cancel"};
  constexpr int kActionX[] = {340, 388};
  constexpr int kActionWidth[] = {40, 76};
  for (int i = 0; i < 2; ++i) {
    const bool selected = selected_key_split_row_ == -1 &&
                          selected_key_split_action_ == i;
    FillRoundedRect(canvas, kActionX[i], 4, kActionWidth[i], 20, 3,
                    selected ? 0 : 32, selected ? 215 : 42,
                    selected ? 255 : 51);
    DrawShinonomeText(canvas, kActionX[i] + 8, 6, kActions[i],
                      selected ? 6 : 170, selected ? 16 : 179,
                      selected ? 20 : 192);
  }

  const bool zones_selected =
      selected_key_split_row_ == 0 && selected_key_split_zones_action_ == 0;
  if (zones_selected) canvas.FillRect(8, 31, 112, 14, 37, 73, 88);
  DrawShinonomeText(canvas, 12, 32,
                    "Zones: " +
                        std::to_string(edited_key_split_settings_.zone_count),
                    zones_selected ? 79 : 244, zones_selected ? 195 : 247,
                    zones_selected ? 247 : 250);

  constexpr const char* kPresetLabels[] = {"Drum", "Drum Kit"};
  constexpr int kPresetX[] = {144, 224};
  constexpr int kPresetWidth[] = {68, 92};
  for (int i = 0; i < 2; ++i) {
    const bool selected = selected_key_split_row_ == 0 &&
                          selected_key_split_zones_action_ == i + 1;
    FillRoundedRect(canvas, kPresetX[i], 30, kPresetWidth[i], 16, 2,
                    selected ? 79 : 32, selected ? 195 : 42,
                    selected ? 247 : 51);
    DrawShinonomeText(canvas, kPresetX[i] + 6, 32, kPresetLabels[i],
                      selected ? 6 : 170, selected ? 16 : 179,
                      selected ? 20 : 192);
  }

  for (int zone = 0; zone < 16; ++zone) {
    const bool enabled = zone < edited_key_split_settings_.zone_count;
    const bool row_selected = selected_key_split_row_ == zone + 1;
    const int selected_field = selected_key_split_column_;
    const int y = 46 + zone * 14;
    if (row_selected) {
      canvas.FillRect(8, y - 1, 456, 14, 37, 73, 88);
    }

    const auto& value = edited_key_split_settings_.zones[zone];
    const std::string zone_label =
        "Zone" + (zone < 9 ? std::string("0") : std::string()) +
        std::to_string(zone + 1) + ":";
    const std::string low = KeySplitNoteName(value.low_note);
    const std::string high = KeySplitNoteName(value.high_note);
    char channel_text[8];
    std::snprintf(channel_text, sizeof(channel_text), "CH:%02d",
                  value.midi_channel);
    const std::string transpose =
        "Trans:" + FormatKeySplitTranspose(value.transpose, value.low_note);
    const std::string line = zone_label + low + "-" + high + " " +
                             channel_text + " " + transpose + " Color:";
    const uint8_t text = enabled ? 244 : 82;
    DrawShinonomeText(canvas, 12, y, line,
                      row_selected ? 234 : text,
                      row_selected ? 248 : (enabled ? 247 : 97),
                      row_selected ? 255 : (enabled ? 250 : 109));
    if (enabled) {
      const auto& color = kZoneColors[value.color];
      canvas.FillRect(432, y + 1, 10, 10, color.r, color.g, color.b);
    }

    // Redraw the selected field over a bright background. Including CH,
    // Trans and Color labels makes the active property unambiguous.
    if (row_selected && selected_field >= 0 &&
        selected_field < kKeySplitFieldsPerZone) {
      std::string prefix;
      std::string selected_text;
      if (selected_field == 0) {
        prefix = zone_label + low + "-";
        selected_text = high;
      } else if (selected_field == 1) {
        prefix = zone_label + low + "-" + high + " ";
        selected_text = channel_text;
      } else if (selected_field == 2) {
        prefix = zone_label + low + "-" + high + " " + channel_text + " ";
        selected_text = transpose;
      } else {
        prefix = zone_label + low + "-" + high + " " + channel_text + " " +
                 transpose + " ";
        selected_text = "Color:";
      }
      const int field_x = 12 + mk2::LcdCanvas::MeasureUtf8Width(prefix, 1);
      const int field_width =
          mk2::LcdCanvas::MeasureUtf8Width(selected_text, 1);
      canvas.FillRect(field_x - 1, y - 1,
                      field_width + (selected_field == 3 ? 14 : 2), 14,
                      79, 195, 247);
      DrawShinonomeText(canvas, field_x, y, selected_text, 6, 16, 20);
      if (selected_field == 3 && enabled) {
        const auto& color = kZoneColors[value.color];
        canvas.FillRect(field_x + field_width + 2, y + 1, 10, 10, color.r,
                        color.g, color.b);
      }
    }
  }
}

void ControllerApp::DrawSetCcPc(mk2::LcdCanvas& canvas, bool right_screen) {
  canvas.FillRect(0, 0, mk2::kLcdWidth, 28, 27, 32, 40);
  DrawShinonomeText(canvas, 12, 6,
                    right_screen ? "Buttons" : "Set CC",
                    244, 247, 250);
  if (!right_screen) {
    constexpr const char* kActions[] = {"OK", "Cancel"};
    constexpr int kActionX[] = {340, 388};
    constexpr int kActionWidth[] = {40, 76};
    for (int i = 0; i < 2; ++i) {
      const bool selected = selected_ccpc_row_ == -1 &&
                            selected_ccpc_action_ == i;
      FillRoundedRect(canvas, kActionX[i], 4, kActionWidth[i], 20, 3,
                      selected ? 0 : 32, selected ? 215 : 42,
                      selected ? 255 : 51);
      DrawShinonomeText(canvas, kActionX[i] + 8, 6, kActions[i],
                        selected ? 6 : 170, selected ? 16 : 179,
                        selected ? 20 : 192);
    }
  }

  DrawShinonomeText(canvas, 12, 36, "CONTROL", 170, 179, 192);
  DrawShinonomeText(canvas, 244, 36, "TYPE", 170, 179, 192);
  DrawShinonomeText(canvas, 350, 36, "VALUE", 170, 179, 192);

  constexpr const char* kLeftNames[] = {
      "Knob 1", "Knob 2", "Knob 3", "Knob 4", "Knob 5",
      "Knob 6", "Knob 7", "Knob 8", "Mod Wheel"};
  constexpr const char* kRightNames[] = {
      "Button 1", "Button 2", "Button 3", "Button 4",
      "Button 5", "Button 6", "Button 7", "Button 8"};
  const int count = right_screen ? static_cast<int>(std::size(kRightNames))
                                 : static_cast<int>(std::size(kLeftNames));
  const int base_index = right_screen ? 9 : 0;
  const bool selected_screen =
      selected_ccpc_column_ == (right_screen ? 1 : 0);
  for (int row = 0; row < count; ++row) {
    const int y = 58 + row * 22;
    const auto& assignment = edited_control_assignments_[base_index + row];
    const bool selected_row = selected_ccpc_row_ == row && selected_screen;
    DrawShinonomeText(canvas, 12, y,
                      right_screen ? kRightNames[row] : kLeftNames[row],
                      selected_row ? 234 : 244,
                      selected_row ? 248 : 247,
                      selected_row ? 255 : 250);

    const std::string type_text = "CC";
    const int value = assignment.cc;
    char value_text[24];
    std::snprintf(value_text, sizeof(value_text), "%03d", value);
    const bool value_selected = selected_row;
    if (value_selected) canvas.FillRect(342, y - 2, 126, 18, 79, 195, 247);
    DrawShinonomeText(canvas, 244, y, type_text,
                      79, 195, 247);
    DrawShinonomeText(canvas, 350, y, value_text,
                      value_selected ? 6 : 244,
                      value_selected ? 16 : 247,
                      value_selected ? 20 : 250);
  }
  if (!ccpc_status_.empty()) {
    DrawShinonomeText(canvas, 12, 250, ccpc_status_, 242, 184, 75);
  }
}

void ControllerApp::Run() {
  running_.store(true);
  DrawStartupScreens();
  if (router_) router_->Start();
  sequencer_->SetTempoBpm(120.0);
  sequencer_->Start();
  PollHidLoop();
}

void ControllerApp::Stop() {
  if (!running_.exchange(false)) return;
  if (sequencer_) sequencer_->Stop();
  if (router_) router_->Stop();
}

void ControllerApp::PollHidLoop() {
  while (running_.load()) {
    auto report = hid_device_->ReadReport(/*timeout_ms=*/50);
    if (report.has_value()) {
      HandleHidReport(*report);
      previous_hid_report_ = *report;
    }
    ApplyPendingUiAction();
    ApplyPendingMidiLogRedraw();
    ApplyPendingMidiControls();
  }
}

void ControllerApp::OnMk2MidiMessage(const mk2::MidiMessage& message) {
  if (message.kind == mk2::MidiMessageKind::kControlChange) {
    const int function_button =
        message.data1 - mk2::kObservedFunctionButtonCcBase;
    if (function_button >= 0 &&
        function_button <
            static_cast<int>(mk2::kLcdFunctionButtonLedIds.size())) {
      if (message.data2 > 0 && function_button < 3) {
        pending_ui_action_.store(function_button);
      }
      return;
    }
  }

  if (router_ && router_->IsMk2ToSeqtrakForwardingEnabled()) {
    AppendMidiLog(message);
  }

  if (message.kind == mk2::MidiMessageKind::kControlChange) {
    const int knob = message.data1 - mk2::kDefaultKnobCcBase;
    if (knob >= 0 && knob < 8) {
      std::fprintf(stderr,
                   "controller_app: MIDI Knob%d CC=0x%02x value=%d\n",
                   knob + 1, message.data1, message.data2);
      pending_knob_cc_[knob].store(message.data2);
    }
  }
}

void ControllerApp::ApplyPendingUiAction() {
  const int action = pending_ui_action_.exchange(-1);
  if (action < 0 || action > static_cast<int>(ActionId::kSetting)) return;
  if (current_screen_ == ScreenId::kMidiLog &&
      action == static_cast<int>(ActionId::kPlay)) {
    ReturnToControllerHome();
    DrawLeftLcdUi();
    return;
  }
  ActivateAction(static_cast<ActionId>(action));
}

void ControllerApp::ActivateAction(ActionId action) {
  switch (action) {
    case ActionId::kPlay:
      if (router_) {
        router_->SetMk2ToSeqtrakForwardingEnabled(true);
      }
      {
        std::lock_guard<std::mutex> lock(midi_log_mutex_);
        midi_log_lines_.clear();
      }
      current_screen_ = ScreenId::kMidiLog;
      selected_home_button_ = 0;
      std::fprintf(stderr,
                   "controller_app: Play: MIDI thru enabled, screen -> MIDI "
                   "LOG\n");
      break;
    case ActionId::kSoundSelect:
      current_screen_ = ScreenId::kSoundSelect;
      lcd_ui_mode_ = LcdUiMode::kTrackSelect;
      selected_variation_item_ = selected_track_ + 1;
      variation_status_.clear();
      std::fprintf(stderr, "controller_app: screen -> Sound Select\n");
      break;
    case ActionId::kSetting:
      current_screen_ = ScreenId::kSettings;
      selected_settings_item_ = 0;
      std::fprintf(stderr, "controller_app: screen -> Settings\n");
      break;
  }
  DrawLeftLcdUi();
}

void ControllerApp::ReturnToControllerHome() {
  current_screen_ = ScreenId::kControllerHome;
  selected_home_button_ = 0;
  std::fprintf(stderr, "controller_app: screen -> Controller Home\n");
}

void ControllerApp::OpenSelectedSoundDestination() {
  selected_sound_category_item_ = 0;
  sound_list_for_drum_kit_ = false;
  if (selected_track_ == 9) {
    current_screen_ = ScreenId::kDxSoundCategory;
  } else if (selected_track_ == 10) {
    current_screen_ = ScreenId::kSamplerSoundCategory;
  } else {
    switch (track_types_[selected_track_]) {
      case 0:
        current_screen_ = ScreenId::kDrumSoundCategory;
        break;
      case 1:
        current_screen_ = ScreenId::kDrumKit;
        selected_drum_kit_item_ = 0;
        break;
      case 2:
        current_screen_ = ScreenId::kSynthSoundCategory;
        break;
      default:
        return;
    }
  }
  std::fprintf(stderr, "controller_app: Variation 01 -> sound screen %d\n",
               static_cast<int>(current_screen_));
}

void ControllerApp::ReturnToVariation() {
  current_screen_ = ScreenId::kSoundSelect;
  lcd_ui_mode_ = LcdUiMode::kTrackSelect;
  selected_variation_item_ = selected_track_ + 1;
  std::fprintf(stderr, "controller_app: sound screen -> Variation 01\n");
}

bool ControllerApp::ApplySelectedSound() {
  const auto sounds = FilteredSoundPresets(selected_sound_kind_,
                                           selected_sound_category_index_);
  if (selected_sound_list_item_ < 0 ||
      selected_sound_list_item_ >= static_cast<int>(sounds.size())) {
    return false;
  }
  const auto& sound = *sounds[selected_sound_list_item_];
  const int channel = sound_list_for_drum_kit_
                          ? selected_drum_kit_part_ + 1
                          : seqtrak::kTracks[selected_track_].midi_channel;
  if (!SendSoundPreset(selected_sound_kind_, sound.number, channel,
                       sound_list_for_drum_kit_ ? selected_drum_kit_part_
                                                : -1)) {
    return false;
  }

  if (sound_list_for_drum_kit_) {
    drum_kit_sound_names_[selected_drum_kit_part_] = sound.name;
    drum_kit_sound_numbers_[selected_drum_kit_part_] = sound.number;
    current_screen_ = ScreenId::kDrumKit;
    selected_drum_kit_item_ = selected_drum_kit_part_ + 2;
  } else {
    track_sound_names_[selected_track_] = sound.name;
    track_sound_categories_[selected_track_] = sound.category;
    track_sound_numbers_[selected_track_] = sound.number;
    track_sound_kinds_[selected_track_] = selected_sound_kind_;
    ReturnToVariation();
  }
  return true;
}

bool ControllerApp::SendSoundPreset(int kind, uint16_t number, int channel,
                                    int drum_kit_part) {
  const int zero_based_number = number - 1;
  const uint8_t bank_lsb = static_cast<uint8_t>(zero_based_number / 128);
  const uint8_t program = static_cast<uint8_t>(zero_based_number % 128);
  const uint8_t bank_msb = drum_kit_part >= 0
                               ? static_cast<uint8_t>(0x20 + drum_kit_part)
                               : static_cast<uint8_t>(kind == 3 ? 0x3E : 0x3F);

  if (router_ == nullptr ||
      !router_->SendToSeqtrak(mk2::BuildControlChange(
          channel, seqtrak::kCcBankSelectMsb, bank_msb)) ||
      !router_->SendToSeqtrak(mk2::BuildControlChange(
          channel, seqtrak::kCcBankSelectLsb, bank_lsb)) ||
      !router_->SendToSeqtrak(mk2::BuildProgramChange(channel, program))) {
    std::fprintf(stderr, "controller_app: Sound selection MIDI send failed\n");
    return false;
  }

  std::fprintf(stderr,
               "controller_app: sent sound number=%u bank=%02x/%02x "
               "program=%u channel=%d\n",
               number, bank_msb, bank_lsb, program, channel);
  return true;
}

bool ControllerApp::ApplyAllTrackSounds() {
  bool sent_any = false;
  bool success = true;
  bool drum_kit_sent = false;
  for (int track = 0; track < 11; ++track) {
    if (track_sound_kinds_[track] >= 0 && track_sound_kinds_[track] <= 3 &&
        track_sound_numbers_[track] > 0) {
      sent_any = true;
      success &= SendSoundPreset(track_sound_kinds_[track],
                                 track_sound_numbers_[track],
                                 seqtrak::kTracks[track].midi_channel);
    } else if (track_sound_kinds_[track] == 4 && !drum_kit_sent) {
      drum_kit_sent = true;
      for (int part = 0; part < 7; ++part) {
        if (drum_kit_sound_numbers_[part] == 0) continue;
        sent_any = true;
        success &= SendSoundPreset(0, drum_kit_sound_numbers_[part], part + 1,
                                   part);
      }
    }
  }
  variation_status_ = !sent_any ? "No presets assigned"
                                : (success ? "All presets sent"
                                           : "Preset send failed");
  return success && sent_any;
}

void ControllerApp::AppendMidiLog(const mk2::MidiMessage& message) {
  const std::string line = FormatMidiLogLine(message);
  {
    std::lock_guard<std::mutex> lock(midi_log_mutex_);
    if (midi_log_lines_.size() == kMidiLogCapacity) {
      midi_log_lines_.pop_front();
    }
    midi_log_lines_.push_back(line);
  }
  midi_log_redraw_pending_.store(true);
}

void ControllerApp::ApplyPendingMidiLogRedraw() {
  if (!midi_log_redraw_pending_.exchange(false)) return;
  if (current_screen_ == ScreenId::kMidiLog) DrawLeftLcdUi();
}

void ControllerApp::ApplyPendingMidiControls() {
  if (current_screen_ == ScreenId::kAwm2Editor) {
    bool changed = false;
    std::array<uint8_t, 8>* values = nullptr;
    if (awm_page_ < 5) values = &awm_common_[awm_page_];
    else if (awm_page_ < 37)
      values = &awm_elements_[(awm_page_ - 5) / 4][(awm_page_ - 5) % 4];
    else values = &awm_insertions_[awm_page_ - 37];
    const auto page = GetAwmPageSpec(awm_page_);
    for (int knob = 0; knob < 8; ++knob) {
      const int cc = pending_knob_cc_[knob].exchange(-1);
      if (cc < 0 || page.labels[knob][0] == '-') continue;
      (*values)[knob] = static_cast<uint8_t>(std::clamp(cc, 0, 127));
      changed = true;
    }
    if (changed) DrawLeftLcdUi();
    return;
  }
  if (current_screen_ == ScreenId::kFmEditor) {
    bool changed = false;
    for (int knob = 0; knob < 8; ++knob) {
      const int cc_value = pending_knob_cc_[knob].exchange(-1);
      if (cc_value < 0 || kFmPages[fm_page_].labels[knob][0] == '-') continue;
      uint8_t* value = nullptr;
      if (fm_page_ == 0) {
        value = &fm_common_[knob];
      } else if (fm_page_ <= 12) {
        const int op = (fm_page_ - 1) / 3;
        const int group = (fm_page_ - 1) % 3;
        value = &fm_operators_[op][group * 8 + knob];
      } else {
        value = &fm_common_[(fm_page_ == 13 ? 8 : 16) + knob];
      }
      *value = static_cast<uint8_t>(std::clamp(cc_value, 0, 127));
      std::fprintf(stderr,
                   "controller_app: FM page=%d Knob%d %s=%d (MIDI CC)\n",
                   fm_page_ + 1, knob + 1, kFmPages[fm_page_].labels[knob],
                   cc_value);
      changed = true;
    }
    if (changed) {
      fm_status_.clear();
      DrawLeftLcdUi();
    }
    return;
  }
  if (current_screen_ != ScreenId::kSoundSelect ||
      lcd_ui_mode_ != LcdUiMode::kTrackDetail) {
    for (auto& value : pending_knob_cc_) value.store(-1);
    return;
  }

  bool changed = false;
  const int volume_cc = pending_knob_cc_[0].exchange(-1);
  if (volume_cc >= 0) {
    track_volumes_[selected_track_] =
        std::clamp((volume_cc * kVolumeMax + 63) / 127, kVolumeMin,
                   kVolumeMax);
    std::fprintf(stderr, "controller_app: Knob1 applied: track=%d Volume=%d\n",
                 selected_track_ + 1, track_volumes_[selected_track_]);
    changed = true;
  }

  const int pan_cc = pending_knob_cc_[1].exchange(-1);
  if (pan_cc >= 0) {
    if (pan_rebased_after_reset_) {
      if (last_pan_midi_value_ >= 0) {
        const int delta = WrappedDelta(last_pan_midi_value_, pan_cc, 128);
        track_pans_[selected_track_] =
            std::clamp(track_pans_[selected_track_] + delta, kPanMin,
                       kPanMax);
      }
      // With no previous CC, the first value only establishes the new
      // physical reference and leaves the reset value at zero.
    } else {
      // Before a reset, MIDI midpoint 64 is center. Map 0..127 to -50..+50.
      track_pans_[selected_track_] =
          std::clamp(((pan_cc - 64) * kPanMax) / 63, kPanMin, kPanMax);
    }
    last_pan_midi_value_ = pan_cc;
    std::fprintf(stderr, "controller_app: Knob2 applied: track=%d Pan=%d\n",
                 selected_track_ + 1, track_pans_[selected_track_]);
    changed = true;
  }

  if (changed) DrawLeftLcdUi();
}

void ControllerApp::HandleHidReport(const std::vector<uint8_t>& report) {
  if (report.empty()) return;
  if (report[0] != mk2::kHidReportInput) {
    std::fprintf(stderr,
                 "controller_app: non-0x01 HID report id=0x%02x len=%zu:",
                 report[0], report.size());
    for (uint8_t byte : report) std::fprintf(stderr, " %02x", byte);
    std::fprintf(stderr, "\n");
    return;
  }

  // Knob-map diagnostics: report touch transitions even when the associated
  // value does not change, and show every raw HID byte that changed. This
  // makes it possible to correct the map on firmware whose layout differs
  // from the currently documented byte 7 / bytes 10..25 layout.
  if (!previous_hid_report_.empty() &&
      previous_hid_report_.size() == report.size()) {
    const uint8_t previous_touch =
        previous_hid_report_.size() >
                static_cast<size_t>(mk2::kInputByteKnobTouch)
            ? previous_hid_report_[mk2::kInputByteKnobTouch]
            : 0;
    const uint8_t current_touch =
        report.size() > static_cast<size_t>(mk2::kInputByteKnobTouch)
            ? report[mk2::kInputByteKnobTouch]
            : 0;
    if (previous_touch != current_touch) {
      std::fprintf(stderr,
                   "controller_app: knob touch byte: 0x%02x -> 0x%02x\n",
                   previous_touch, current_touch);
      for (int knob = 0; knob < mk2::kKnobCount; ++knob) {
        const uint8_t mask = mk2::kKnobTouchMasks[knob];
        const bool was_touched = (previous_touch & mask) != 0;
        const bool is_touched = (current_touch & mask) != 0;
        if (was_touched != is_touched) {
          std::fprintf(stderr, "controller_app: Knob%d touch %s\n", knob + 1,
                       is_touched ? "ON" : "OFF");
          if (is_touched) {
            last_touched_knob_ = knob;

            // Knob 2 double-touch is a quick Pan-center gesture. Rotation
            // values themselves arrive as MIDI CC 0x0F; only capacitive
            // touch gestures come from this HID report.
            if (knob == 1 && lcd_ui_mode_ != LcdUiMode::kTrackSelect) {
              const auto now = std::chrono::steady_clock::now();
              const auto interval = now - last_knob2_touch_;
              if (last_knob2_touch_ !=
                      std::chrono::steady_clock::time_point{} &&
                  interval >= kKnobDoubleTouchMin &&
                  interval <= kKnobDoubleTouchMax) {
                track_pans_[selected_track_] = 0;
                // CC 0x0F is absolute. Rebase subsequent turns on the
                // physical value seen before this reset.
                pan_rebased_after_reset_ = true;
                last_knob2_touch_ = {};
                std::fprintf(
                    stderr,
                    "controller_app: Knob2 double-touch: track=%d Pan=0\n",
                    selected_track_ + 1);
                DrawLeftLcdUi();
              } else {
                last_knob2_touch_ = now;
              }
            }
          }
        }
      }
    }

    bool printed_prefix = false;
    for (size_t byte = 1; byte < report.size(); ++byte) {
      if (previous_hid_report_[byte] == report[byte]) continue;
      if (!printed_prefix) {
        std::fprintf(stderr, "controller_app: HID changed bytes:");
        printed_prefix = true;
      }
      std::fprintf(stderr, " [%zu]=%02x->%02x", byte,
                   previous_hid_report_[byte], report[byte]);
    }
    if (printed_prefix) std::fprintf(stderr, "\n");
  }

  // The first three Function buttons are global UI actions. The remaining
  // buttons are sent to SEQTRAK only after Play has enabled MIDI thru.
  for (size_t i = 0; i < mk2::kFunctionButtonMasks.size(); ++i) {
    uint8_t mask = mk2::kFunctionButtonMasks[i];
    bool was_down = !previous_hid_report_.empty() &&
                    static_cast<size_t>(mk2::kInputByteFunctionButtons) <
                        previous_hid_report_.size() &&
                    (previous_hid_report_[mk2::kInputByteFunctionButtons] &
                     mask);
    bool is_down =
        static_cast<size_t>(mk2::kInputByteFunctionButtons) < report.size() &&
        (report[mk2::kInputByteFunctionButtons] & mask);
    if (is_down && !was_down) {
      if (current_screen_ == ScreenId::kMidiLog && i == 0) {
        ReturnToControllerHome();
        DrawLeftLcdUi();
      } else if (i < std::size(kHomeButtons)) {
        ActivateAction(static_cast<ActionId>(i));
      } else if (router_ &&
                 router_->IsMk2ToSeqtrakForwardingEnabled()) {
        uint8_t cc = static_cast<uint8_t>(
            mk2::kDefaultFunctionButtonCcBase + static_cast<int>(i));
        router_->SendToSeqtrak(
            mk2::BuildControlChange(kControlChannel, cc, 127));
      }
    }
  }

  bool ui_changed = false;
  // FM Editor follows the Penpot navigation contract: PageLeft/PageRight
  // (panel buttons) and jog left/right move between the 15 pages.
  if ((current_screen_ == ScreenId::kFmEditor ||
       current_screen_ == ScreenId::kAwm2Editor) && report.size() > 3) {
    const uint8_t previous_panel = previous_hid_report_.size() > 3
                                       ? previous_hid_report_[3]
                                       : 0;
    if (report[3] == 0x80 && previous_panel != 0x80) {
      if (current_screen_ == ScreenId::kFmEditor) {
        fm_page_ = (fm_page_ + 14) % 15;
        selected_fm_header_action_ = fm_page_ == 0 ? 1 : 0;
        fm_status_.clear();
      } else {
        awm_page_ = (awm_page_ + 38) % 39;
      }
      ui_changed = true;
    } else if (report[3] == 0x20 && previous_panel != 0x20) {
      if (current_screen_ == ScreenId::kFmEditor) {
        fm_page_ = (fm_page_ + 1) % 15;
        selected_fm_header_action_ = fm_page_ == 0 ? 1 : 0;
        fm_status_.clear();
      } else {
        awm_page_ = (awm_page_ + 1) % 39;
      }
      ui_changed = true;
    }
  }
  const uint8_t current_jog_control =
      report.size() > static_cast<size_t>(mk2::kInputByteJogControl)
          ? report[mk2::kInputByteJogControl]
          : mk2::kJogIdle;
  const uint8_t previous_jog_control =
      previous_hid_report_.size() >
              static_cast<size_t>(mk2::kInputByteJogControl)
          ? previous_hid_report_[mk2::kInputByteJogControl]
          : mk2::kJogIdle;

  // Byte 30 is only a meaningful rotation counter while the jog wheel is
  // being touched. Other control activity can arrive in the same HID report,
  // so interpreting every byte-30 change as a jog turn can move the UI while
  // an LCD knob is being operated.
  if (!previous_hid_report_.empty() &&
      (current_jog_control == mk2::kJogTouch ||
       previous_jog_control == mk2::kJogTouch) &&
      report.size() > static_cast<size_t>(mk2::kInputByteJogTurn) &&
      previous_hid_report_.size() > static_cast<size_t>(mk2::kInputByteJogTurn)) {
    int delta = WrappedDelta(
        previous_hid_report_[mk2::kInputByteJogTurn] & 0x0F,
        report[mk2::kInputByteJogTurn] & 0x0F, 16);
    if (delta != 0) {
      if (current_screen_ == ScreenId::kKeySplit) {
        ChangeKeySplitValue(delta);
      } else if (current_screen_ == ScreenId::kSetCcPc) {
        ChangeSetCcPcValue(delta);
      } else {
        MoveJogSelection(delta);
      }
      ui_changed = true;
    }
  }

  if (report.size() > static_cast<size_t>(mk2::kInputByteJogControl)) {
    uint8_t current = current_jog_control;
    uint8_t previous = previous_jog_control;
    if (current != previous) {
      if (current == mk2::kJogLeft) {
        if (current_screen_ == ScreenId::kAwm2Editor && awm_page_ == 0 &&
            awm_overview_mode_ != AwmOverviewMode::kOverview) {
          if (awm_overview_mode_ == AwmOverviewMode::kCategory) {
            if (selected_awm_category_ >= 8) selected_awm_category_ -= 8;
          } else {
            const int page_start = (selected_awm_sound_ / 16) * 16;
            if (selected_awm_sound_ >= page_start + 8)
              selected_awm_sound_ -= 8;
          }
        } else if (current_screen_ == ScreenId::kFmEditor) {
          fm_page_ = (fm_page_ + 14) % 15;
          selected_fm_header_action_ = fm_page_ == 0 ? 1 : 0;
          fm_status_.clear();
        } else if (current_screen_ == ScreenId::kAwm2Editor) {
          awm_page_ = (awm_page_ + 38) % 39;
        } else if (current_screen_ == ScreenId::kKeySplit) {
          MoveKeySplitColumn(-1);
        } else if (current_screen_ == ScreenId::kSetCcPc) {
          MoveSetCcPcColumn(-1);
        } else {
          MoveJogSelection(-1);
        }
        ui_changed = true;
      } else if (current == mk2::kJogRight) {
        if (current_screen_ == ScreenId::kAwm2Editor && awm_page_ == 0 &&
            awm_overview_mode_ != AwmOverviewMode::kOverview) {
          if (awm_overview_mode_ == AwmOverviewMode::kCategory) {
            if (selected_awm_category_ < 8)
              selected_awm_category_ =
                  std::min(selected_awm_category_ + 8, 14);
          } else {
            const auto sounds =
                FilteredSoundPresets(1, selected_awm_category_);
            const int page_start = (selected_awm_sound_ / 16) * 16;
            if (selected_awm_sound_ < page_start + 8 &&
                selected_awm_sound_ + 8 < static_cast<int>(sounds.size()))
              selected_awm_sound_ += 8;
          }
        } else if (current_screen_ == ScreenId::kFmEditor) {
          fm_page_ = (fm_page_ + 1) % 15;
          selected_fm_header_action_ = fm_page_ == 0 ? 1 : 0;
          fm_status_.clear();
        } else if (current_screen_ == ScreenId::kAwm2Editor) {
          awm_page_ = (awm_page_ + 1) % 39;
        } else if (current_screen_ == ScreenId::kKeySplit) {
          MoveKeySplitColumn(1);
        } else if (current_screen_ == ScreenId::kSetCcPc) {
          MoveSetCcPcColumn(1);
        } else {
          MoveJogSelection(1);
        }
        ui_changed = true;
      } else if (current == mk2::kJogUp) {
        if (current_screen_ == ScreenId::kAwm2Editor && awm_page_ == 0) {
          MoveJogSelection(-1);
          ui_changed = true;
        } else if (current_screen_ == ScreenId::kKeySplit) {
          MoveKeySplitRow(-1);
          ui_changed = true;
        } else if (current_screen_ == ScreenId::kSetCcPc) {
          MoveSetCcPcRow(-1);
          ui_changed = true;
        }
      } else if (current == mk2::kJogDown) {
        if (current_screen_ == ScreenId::kAwm2Editor && awm_page_ == 0) {
          MoveJogSelection(1);
          ui_changed = true;
        } else if (current_screen_ == ScreenId::kKeySplit) {
          MoveKeySplitRow(1);
          ui_changed = true;
        } else if (current_screen_ == ScreenId::kSetCcPc) {
          MoveSetCcPcRow(1);
          ui_changed = true;
        }
      } else if (current == mk2::kJogPress) {
        ConfirmJogSelection();
        ui_changed = true;
      }
    }
  }

  // Eight LCD knobs -> CC14..21, scaled from the observed 0..999 range down
  // to the standard MIDI 0..127 CC range.
  if (report.size() > static_cast<size_t>(mk2::kInputKnobValueBase +
                                           2 * mk2::kKnobCount - 1) &&
      !previous_hid_report_.empty() &&
      previous_hid_report_.size() ==
          report.size()) {
    for (int knob = 0; knob < mk2::kKnobCount; ++knob) {
      int offset = mk2::kInputKnobValueBase + knob * 2;
      uint16_t prev_value = static_cast<uint16_t>(
          previous_hid_report_[offset] |
          (previous_hid_report_[offset + 1] << 8));
      uint16_t cur_value = static_cast<uint16_t>(
          report[offset] | (report[offset + 1] << 8));
      if (prev_value == cur_value) continue;

      const uint8_t touch_mask = mk2::kKnobTouchMasks[knob];
      const bool knob_is_touched =
          report.size() > static_cast<size_t>(mk2::kInputByteKnobTouch) &&
          (report[mk2::kInputByteKnobTouch] & touch_mask) != 0;
      const bool knob_was_touched =
          previous_hid_report_.size() >
                  static_cast<size_t>(mk2::kInputByteKnobTouch) &&
          (previous_hid_report_[mk2::kInputByteKnobTouch] & touch_mask) != 0;
      const bool knob_is_being_operated =
          knob_is_touched || knob_was_touched;
      // `knob` is the value slot described by the current protocol map.
      // Some units deliver that slot after touch has already gone low (and
      // field logs suggest the slot order can differ), so use the last
      // physical touch as the authoritative control identity.
      const int physical_knob =
          last_touched_knob_ >= 0 ? last_touched_knob_ : knob;
      const int delta =
          WrappedDelta(prev_value, cur_value, mk2::kKnobValueModulo);

      std::fprintf(stderr,
                   "controller_app: value_slot=%d physical_Knob%d "
                   "touch=%s (previous=%s) "
                   "raw=%u -> %u delta=%d "
                   "touch_byte=0x%02x ui_mode=%d\n",
                   knob + 1, physical_knob + 1,
                   knob_is_touched ? "yes" : "no",
                   knob_was_touched ? "yes" : "no", prev_value, cur_value,
                   delta,
                   report.size() >
                           static_cast<size_t>(mk2::kInputByteKnobTouch)
                       ? report[mk2::kInputByteKnobTouch]
                       : 0,
                   static_cast<int>(lcd_ui_mode_));

      if (current_screen_ == ScreenId::kFmEditor &&
          (knob_is_being_operated || last_touched_knob_ >= 0)) {
        const int fm_knob = std::clamp(physical_knob, 0, 7);
        uint8_t* value = nullptr;
        if (fm_page_ == 0) {
          value = &fm_common_[fm_knob];
        } else if (fm_page_ <= 12) {
          const int op = (fm_page_ - 1) / 3;
          const int group = (fm_page_ - 1) % 3;
          value = &fm_operators_[op][group * 8 + fm_knob];
        } else {
          value = &fm_common_[(fm_page_ == 13 ? 8 : 16) + fm_knob];
        }
        if (kFmPages[fm_page_].labels[fm_knob][0] != '-') {
          *value = static_cast<uint8_t>(std::clamp(
              static_cast<int>(*value) + std::clamp(delta, -10, 10), 0, 127));
          fm_status_.clear();
          ui_changed = true;
        }
        continue;
      }
      if (current_screen_ == ScreenId::kAwm2Editor &&
          (knob_is_being_operated || last_touched_knob_ >= 0)) {
        const int awm_knob = std::clamp(physical_knob, 0, 7);
        std::array<uint8_t, 8>* values = nullptr;
        if (awm_page_ < 5) values = &awm_common_[awm_page_];
        else if (awm_page_ < 37)
          values = &awm_elements_[(awm_page_ - 5) / 4][(awm_page_ - 5) % 4];
        else values = &awm_insertions_[awm_page_ - 37];
        if (GetAwmPageSpec(awm_page_).labels[awm_knob][0] != '-') {
          (*values)[awm_knob] = static_cast<uint8_t>(std::clamp(
              static_cast<int>((*values)[awm_knob]) +
                  std::clamp(delta, -10, 10), 0, 127));
          ui_changed = true;
        }
        continue;
      }

      // In the track settings screens, LCD knob 1 edits Volume (0..100) and
      // knob 2 edits Pan (-50..50). Positive hardware deltas are clockwise;
      // negative deltas are counter-clockwise.
      if (current_screen_ == ScreenId::kSoundSelect &&
          lcd_ui_mode_ == LcdUiMode::kTrackDetail && physical_knob < 2 &&
          (knob_is_being_operated || last_touched_knob_ >= 0)) {
        int step = std::clamp(delta, -10, 10);
        if (physical_knob == 0) {
          int& volume = track_volumes_[selected_track_];
          volume = std::clamp(volume + step, kVolumeMin, kVolumeMax);
          std::fprintf(stderr,
                       "controller_app: Knob1 applied: track=%d Volume=%d\n",
                       selected_track_ + 1, volume);
        } else {
          int& pan = track_pans_[selected_track_];
          pan = std::clamp(pan + step, kPanMin, kPanMax);
          std::fprintf(stderr,
                       "controller_app: Knob2 applied: track=%d Pan=%d\n",
                       selected_track_ + 1, pan);
        }
        ui_changed = true;
        continue;
      } else if (physical_knob < 2) {
        std::fprintf(stderr,
                     "controller_app: Knob%d not applied (%s%s)\n",
                     physical_knob + 1,
                     lcd_ui_mode_ == LcdUiMode::kTrackSelect
                         ? "track selection screen"
                         : "",
                     !knob_is_being_operated ? "touch bit is off" : "");
      }

      uint8_t cc =
          static_cast<uint8_t>(mk2::kDefaultKnobCcBase + knob);
      uint8_t cc_value = static_cast<uint8_t>(
          (static_cast<uint32_t>(cur_value) * 127) / (mk2::kKnobValueModulo - 1));
      if (router_) {
        router_->SendToSeqtrak(
            mk2::BuildControlChange(kControlChannel, cc, cc_value));
      }
    }
  }

  if (ui_changed) DrawLeftLcdUi();
}

void ControllerApp::MoveJogSelection(int delta) {
  if (current_screen_ == ScreenId::kAwm2Editor && awm_page_ == 0) {
    if (awm_overview_mode_ == AwmOverviewMode::kOverview) {
      selected_awm_element_ =
          (selected_awm_element_ + delta % 8 + 8) % 8;
    } else if (awm_overview_mode_ == AwmOverviewMode::kCategory) {
      selected_awm_category_ =
          (selected_awm_category_ + delta % 15 + 15) % 15;
    } else {
      const int count = static_cast<int>(
          FilteredSoundPresets(1, selected_awm_category_).size());
      if (count > 0)
        selected_awm_sound_ =
            (selected_awm_sound_ + delta % count + count) % count;
    }
    return;
  }
  if (current_screen_ == ScreenId::kControllerHome) {
    constexpr int count = static_cast<int>(std::size(kHomeButtons));
    selected_home_button_ =
        (selected_home_button_ + delta % count + count) % count;
    return;
  }
  if (current_screen_ == ScreenId::kSettings) {
    constexpr int count = 6;
    selected_settings_item_ =
        (selected_settings_item_ + delta % count + count) % count;
    return;
  }
  if (current_screen_ == ScreenId::kSeqtrakTrackSelect) {
    constexpr int count = static_cast<int>(seqtrak::kTrackCount) + 1;
    selected_seqtrak_track_ =
        ((selected_seqtrak_track_ + 1 + delta % count + count) % count) - 1;
    return;
  }
  if (current_screen_ == ScreenId::kFmEditor) {
    selected_fm_header_action_ = fm_page_ == 0
                                     ? (selected_fm_header_action_ +
                                        delta % 2 + 2) % 2
                                     : 0;
    return;
  }
  if (current_screen_ == ScreenId::kKeySplit) {
    return;
  }
  if (current_screen_ == ScreenId::kSetCcPc) {
    return;
  }
  if (current_screen_ == ScreenId::kSoundList) {
    const int sound_count = static_cast<int>(
        FilteredSoundPresets(selected_sound_kind_,
                             selected_sound_category_index_)
            .size());
    const int count = sound_count + 1;
    selected_sound_list_item_ =
        ((selected_sound_list_item_ + 1 + delta % count + count) % count) - 1;
    return;
  }
  if (current_screen_ == ScreenId::kDrumSoundCategory ||
      current_screen_ == ScreenId::kSynthSoundCategory ||
      current_screen_ == ScreenId::kDxSoundCategory ||
      current_screen_ == ScreenId::kSamplerSoundCategory ||
      current_screen_ == ScreenId::kDrumKit) {
    if (current_screen_ == ScreenId::kDrumKit) {
      constexpr int count = 10;
      selected_drum_kit_item_ =
          (selected_drum_kit_item_ + delta % count + count) % count;
    } else {
      constexpr int count = 16;
      selected_sound_category_item_ =
          (selected_sound_category_item_ + delta % count + count) % count;
    }
    return;
  }
  if (current_screen_ != ScreenId::kSoundSelect) return;

  if (lcd_ui_mode_ == LcdUiMode::kTrackSelect) {
    constexpr int count = static_cast<int>(seqtrak::kTrackCount) + 2;
    selected_variation_item_ =
        (selected_variation_item_ + delta % count + count) % count;
    if (selected_variation_item_ >= 1 && selected_variation_item_ <= 11) {
      selected_track_ = selected_variation_item_ - 1;
    }
  } else if (lcd_ui_mode_ == LcdUiMode::kTrackTypeSelect) {
    constexpr int count = 3;
    int& type = track_types_[selected_track_];
    type = (type + delta % count + count) % count;
  }
}

void ControllerApp::MoveKeySplitRow(int delta) {
  // Header + Zones + enabled Zone rows. Disabled rows are intentionally
  // skipped because the Zone count controls whether they can be edited.
  const int count = edited_key_split_settings_.zone_count + 2;
  selected_key_split_row_ =
      ((selected_key_split_row_ + 1 + delta % count + count) % count) - 1;
}

void ControllerApp::MoveKeySplitColumn(int delta) {
  if (selected_key_split_row_ == -1) {
    selected_key_split_action_ =
        std::clamp(selected_key_split_action_ + delta, 0, 1);
  } else if (selected_key_split_row_ == 0) {
    selected_key_split_zones_action_ =
        std::clamp(selected_key_split_zones_action_ + delta, 0, 2);
  } else {
    selected_key_split_column_ = std::clamp(
        selected_key_split_column_ + delta, 0, kKeySplitFieldsPerZone - 1);
  }
}

void ControllerApp::ChangeKeySplitValue(int delta) {
  if (selected_key_split_row_ == 0) {
    if (selected_key_split_zones_action_ != 0) return;
    const int previous_count = edited_key_split_settings_.zone_count;
    const int new_count = std::clamp(previous_count + delta, 1, 16);
    edited_key_split_settings_.zone_count = new_count;
    if (new_count > previous_count) {
      // Give newly enabled Zones useful, evenly spaced boundaries instead
      // of initially leaving the first Zone at G9 (which would take many
      // wheel turns to correct).
      constexpr int kNoteCount =
          kKeySplitHighestNote - kKeySplitLowestNote + 1;
      for (int i = 0; i < new_count; ++i) {
        edited_key_split_settings_.zones[i].high_note =
            i == new_count - 1
                ? kKeySplitHighestNote
                : kKeySplitLowestNote +
                      (kNoteCount * (i + 1)) / new_count - 1;
      }
    }
    NormalizeKeySplitRanges(edited_key_split_settings_);
    return;
  }
  if (selected_key_split_row_ < 1) return;
  const int zone_index = selected_key_split_row_ - 1;
  const int field = selected_key_split_column_;
  auto& zone = edited_key_split_settings_.zones[zone_index];
  switch (field) {
    case 0:
      // Leave at least one note for every following enabled Zone. The last
      // Zone always ends at MIDI note 127 (G9), covering C1..G9.
      if (zone_index + 1 < edited_key_split_settings_.zone_count) {
        const int remaining = edited_key_split_settings_.zone_count -
                              zone_index - 1;
        zone.high_note = std::clamp(zone.high_note + delta, zone.low_note,
                                    kKeySplitHighestNote - remaining);
        NormalizeKeySplitRanges(edited_key_split_settings_);
      }
      break;
    case 1:
      zone.midi_channel = std::clamp(zone.midi_channel + delta, 1, 16);
      break;
    case 2:
      zone.transpose = std::clamp(zone.transpose + delta, -64, 63);
      break;
    case 3: {
      constexpr int count = static_cast<int>(std::size(kZoneColors));
      zone.color = (zone.color + delta % count + count) % count;
      break;
    }
  }
}

void ControllerApp::MoveSetCcPcRow(int delta) {
  const bool right_screen = selected_ccpc_column_ == 1;
  const int row_count = right_screen ? 8 : 9;
  const int count = row_count + 1;  // Header plus control rows.
  selected_ccpc_row_ =
      ((selected_ccpc_row_ + 1 + delta % count + count) % count) - 1;
}

void ControllerApp::MoveSetCcPcColumn(int delta) {
  if (selected_ccpc_row_ == -1) {
    selected_ccpc_action_ = std::clamp(selected_ccpc_action_ + delta, 0, 1);
    return;
  }
  selected_ccpc_column_ = std::clamp(selected_ccpc_column_ + delta, 0, 1);
  // The left LCD has one additional row (Mod Wheel). Moving from it to the
  // right LCD keeps the selection on the final available Button row.
  if (selected_ccpc_column_ == 1) {
    selected_ccpc_row_ = std::min(selected_ccpc_row_, 7);
  }
}

void ControllerApp::ChangeSetCcPcValue(int delta) {
  if (selected_ccpc_row_ < 0) return;
  ccpc_status_.clear();
  const bool right_screen = selected_ccpc_column_ == 1;
  const int index = (right_screen ? 9 : 0) + selected_ccpc_row_;
  auto& assignment = edited_control_assignments_[index];
  assignment.cc = std::clamp(assignment.cc + delta, 0, 127);
}

bool ControllerApp::ApplyControlAssignments(
    const std::array<ControlAssignment, 17>& assignments) {
  std::vector<uint8_t> controls;
  controls.reserve(204);
  controls.push_back(mk2::kHidReportControlsAssign);
  // A1 stores the eight Button entries before the eight Knob entries.
  for (int i = 0; i < 8; ++i) {
    const uint8_t value =
        static_cast<uint8_t>(assignments[9 + i].cc);
    controls.insert(controls.end(),
                    {mk2::kControlModeMidiCc, value, 0x00,
                     mk2::kButtonActionTrigger, 0x00, 0x00, 0x7F, 0x00,
                     0x00, 0x00, 0x00, 0x00});
  }
  for (int i = 0; i < 8; ++i) {
    const uint8_t value = static_cast<uint8_t>(assignments[i].cc);
    controls.push_back(mk2::kControlModeMidiCc);
    controls.push_back(value);
    controls.push_back(0x00);  // MIDI channel 1, zero-based.
    controls.insert(controls.end(), mk2::kKnobAssignFixedTail.begin(),
                    mk2::kKnobAssignFixedTail.end());
  }
  controls.insert(controls.end(), 8, 0x0A);  // Blue Button backgrounds.
  controls.insert(controls.end(), 3, 0x00);

  std::vector<uint8_t> sliders;
  sliders.reserve(45);
  sliders.push_back(mk2::kHidReportSlidersAssign);
  // Preserve the Pitch Wheel in pitch-bend mode.
  sliders.insert(sliders.end(),
                 {mk2::kSliderModePitch, 0x00, 0x00, 0x00, 0x00, 0x00,
                  0xFF, 0x3F, 0x00, 0x00, 0x01, 0x00});
  const uint8_t mod_cc = static_cast<uint8_t>(assignments[8].cc);
  sliders.insert(sliders.end(),
                 {mk2::kSliderModeCc, mod_cc, 0x00, 0x20, 0x00, 0x00,
                  0x7F, 0x00, 0x00, 0x00, 0x00, 0x00});
  // Touch Strip remains disabled until its assignment is re-verified.
  sliders.insert(sliders.end(), 12, 0x00);
  sliders.insert(sliders.end(), 8, 0x00);

  if (dry_run_) {
    std::fprintf(stderr, "[dry-run] -> HID Controls A1: %zu bytes\n%s",
                 controls.size(), mk2util::PreviewHexDump(controls).c_str());
    std::fprintf(stderr, "[dry-run] -> HID Sliders A2: %zu bytes\n%s",
                 sliders.size(), mk2util::PreviewHexDump(sliders).c_str());
    ccpc_status_.clear();
    return true;
  }
  if (hid_device_ == nullptr || !hid_device_->IsOpen()) {
    ccpc_status_ = "HID device is unavailable";
    return false;
  }
  if (!hid_device_->WriteReport(controls) ||
      !hid_device_->WriteReport(sliders)) {
    ccpc_status_ = "HID write failed";
    std::fprintf(stderr, "controller_app: control HID write failed: %s\n",
                 hid_device_->last_error().c_str());
    return false;
  }
  ccpc_status_.clear();
  std::fprintf(stderr,
               "controller_app: applied A1 controls and A2 Mod Wheel reports\n");
  return true;
}

bool ControllerApp::LoadControlAssignments() {
  std::ifstream input(kControlAssignmentsPath);
  if (!input) {
    std::fprintf(stderr,
                 "controller_app: no saved control assignments; using defaults\n");
    return false;
  }

  auto loaded = control_assignments_;
  std::string format;
  if (!(input >> format) || format != "S49CC1") {
    std::fprintf(stderr,
                 "controller_app: unsupported control assignments file; "
                 "using defaults\n");
    return false;
  }
  for (auto& assignment : loaded) {
    int cc = 0;
    if (!(input >> cc) || cc < 0 || cc > 127) {
      std::fprintf(stderr,
                   "controller_app: invalid control assignments file; "
                   "using defaults\n");
      return false;
    }
    assignment.cc = cc;
  }
  control_assignments_ = loaded;
  edited_control_assignments_ = loaded;
  std::fprintf(stderr, "controller_app: loaded control assignments from %s\n",
               kControlAssignmentsPath);
  return true;
}

bool ControllerApp::SaveControlAssignments(
    const std::array<ControlAssignment, 17>& assignments) {
  const std::string temporary_path =
      std::string(kControlAssignmentsPath) + ".tmp";
  {
    std::ofstream output(temporary_path, std::ios::trunc);
    if (!output) {
      ccpc_status_ = "Could not open settings file";
      return false;
    }
    output << "S49CC1\n";
    for (const auto& assignment : assignments) {
      output << assignment.cc << '\n';
    }
    if (!output) {
      ccpc_status_ = "Could not write settings file";
      return false;
    }
  }
  if (std::rename(temporary_path.c_str(), kControlAssignmentsPath) != 0) {
    std::remove(temporary_path.c_str());
    ccpc_status_ = "Could not replace settings file";
    return false;
  }
  std::fprintf(stderr, "controller_app: saved control assignments to %s\n",
               kControlAssignmentsPath);
  return true;
}

bool ControllerApp::SaveFmPatch() {
  bool sent = router_ != nullptr;
  if (router_) {
    sent = router_->SendToSeqtrak(mk2::BuildSeqtrakParameterChange(
               0, seqtrak::kAddrDxCommon,
               std::vector<uint8_t>(fm_common_.begin(), fm_common_.end()))) &&
           sent;
    for (int op = 0; op < 4; ++op) {
      const seqtrak::ParamAddress address = {
          seqtrak::kAddrDxOperatorHigh,
          static_cast<uint8_t>(op * 0x10 + 0x09), 0x00};
      sent = router_->SendToSeqtrak(mk2::BuildSeqtrakParameterChange(
                 0, address, std::vector<uint8_t>(fm_operators_[op].begin(),
                                                  fm_operators_[op].end()))) &&
             sent;
    }
  }
  if (!sent) {
    fm_status_ = "SEQTRAK send failed";
    return false;
  }

  const std::string temporary_path = std::string(kFmPatchPath) + ".tmp";
  {
    std::ofstream output(temporary_path, std::ios::trunc);
    if (!output) {
      fm_status_ = "JSON open failed";
      return false;
    }
    output << "{\n  \"format\": \"SEQTRAK_DX_PATCH_V1\",\n";
    output << "  \"common\": [";
    for (size_t i = 0; i < fm_common_.size(); ++i)
      output << (i ? ", " : "") << static_cast<int>(fm_common_[i]);
    output << "],\n  \"operators\": [\n";
    for (size_t op = 0; op < fm_operators_.size(); ++op) {
      output << "    [";
      for (size_t i = 0; i < fm_operators_[op].size(); ++i)
        output << (i ? ", " : "") << static_cast<int>(fm_operators_[op][i]);
      output << "]" << (op + 1 == fm_operators_.size() ? "\n" : ",\n");
    }
    output << "  ]\n}\n";
    if (!output) {
      fm_status_ = "JSON write failed";
      return false;
    }
  }
  if (std::rename(temporary_path.c_str(), kFmPatchPath) != 0) {
    std::remove(temporary_path.c_str());
    fm_status_ = "JSON replace failed";
    return false;
  }
  fm_status_ = "Sent and saved";
  std::fprintf(stderr, "controller_app: DX patch sent and saved to %s\n",
               kFmPatchPath);
  return true;
}

void ControllerApp::LoadDrumKeySplitPreset() {
  // Zone 1/13 leave the Light Guide dark and preserve the outer keyboard
  // ranges. Zones 2..12 map the eleven SEQTRAK tracks to contiguous ranges.
  constexpr int kDrumRangeEnd[] = {
      37, 39, 40, 42, 44, 46, 47, 59, 71, 83, 95,
  };

  edited_key_split_settings_.zone_count = 13;
  auto& low = edited_key_split_settings_.zones[0];
  low.high_note = 35;  // C1-B1
  low.midi_channel = 12;
  low.transpose = 0;
  low.color = 7;  // Light Guide off/black

  int start_note = 36;  // C2
  constexpr int kTargetNote = 60;  // C4
  for (int i = 0; i < 11; ++i) {
    auto& zone = edited_key_split_settings_.zones[i + 1];
    zone.low_note = start_note;
    zone.high_note = kDrumRangeEnd[i];
    zone.midi_channel = i + 1;
    zone.transpose = kTargetNote - start_note;
    zone.color = i % 7;
    start_note = zone.high_note + 1;
  }

  auto& high = edited_key_split_settings_.zones[12];
  high.low_note = 96;   // C7
  high.high_note = 127; // G9
  high.midi_channel = 13;
  high.transpose = 0;
  high.color = 7;
  NormalizeKeySplitRanges(edited_key_split_settings_);
  std::fprintf(stderr, "controller_app: loaded Drum Key Split preset\n");
}

void ControllerApp::LoadDrumKitKeySplitPreset() {
  constexpr int kChannels[] = {4, 1, 8, 9, 10, 11, 5, 6, 7};
  constexpr int kRangeEnd[] = {35, 47, 59, 71, 83, 95, 107, 119, 127};
  constexpr int kTargetNote = 60;  // C4

  edited_key_split_settings_.zone_count = 9;
  int start_note = 24;  // C1
  for (int i = 0; i < 9; ++i) {
    auto& zone = edited_key_split_settings_.zones[i];
    zone.low_note = start_note;
    zone.high_note = kRangeEnd[i];
    zone.midi_channel = kChannels[i];
    zone.transpose = kTargetNote - start_note;
    zone.color = i % 7;
    start_note = zone.high_note + 1;
  }
  NormalizeKeySplitRanges(edited_key_split_settings_);
  std::fprintf(stderr, "controller_app: loaded Drum Kit Key Split preset\n");
}

void ControllerApp::NormalizeKeySplitRanges(KeySplitSettings& settings) {
  settings.zone_count = std::clamp(settings.zone_count, 1, 16);
  int next_start = kKeySplitLowestNote;
  for (int i = 0; i < settings.zone_count; ++i) {
    auto& zone = settings.zones[i];
    zone.low_note = next_start;
    const int remaining = settings.zone_count - i - 1;
    zone.high_note =
        i == settings.zone_count - 1
            ? kKeySplitHighestNote
            : std::clamp(zone.high_note, zone.low_note,
                         kKeySplitHighestNote - remaining);
    next_start = zone.high_note + 1;
  }
}

std::vector<uint8_t> ControllerApp::BuildKeySplitReport(
    const KeySplitSettings& settings) const {
  std::vector<uint8_t> report;
  report.reserve(1 + mk2::kKeyzoneCount * mk2::kKeyzoneEntryLen);
  report.push_back(mk2::kHidReportKeyzones);
  for (int i = 0; i < mk2::kKeyzoneCount; ++i) {
    if (i < settings.zone_count) {
      const auto& zone = settings.zones[i];
      const auto& color = kZoneColors[zone.color];
      report.push_back(static_cast<uint8_t>(zone.high_note));
      report.push_back(static_cast<uint8_t>(zone.transpose));
      report.push_back(static_cast<uint8_t>(zone.midi_channel - 1));
      report.push_back(mk2::kVelocityCurveLinear);
      report.insert(report.end(), color.hid.begin(), color.hid.end());
      report.push_back(0x00);
      report.push_back(0x00);
    } else {
      report.push_back(0x7F);
      report.push_back(0x00);
      report.push_back(0x00);
      report.push_back(mk2::kVelocityCurveZoneOff);
      report.insert(report.end(), mk2::kZoneColorOff.begin(),
                    mk2::kZoneColorOff.end());
      report.push_back(0x00);
      report.push_back(0x00);
    }
  }
  return report;
}

bool ControllerApp::ApplyKeySplitSettings(
    const KeySplitSettings& settings) {
  const auto report = BuildKeySplitReport(settings);
  if (dry_run_) {
    std::fprintf(stderr, "[dry-run] -> HID Key Zones: %zu byte report\n%s",
                 report.size(), mk2util::PreviewHexDump(report).c_str());
    return true;
  }
  if (hid_device_ == nullptr || !hid_device_->IsOpen()) {
    std::fprintf(stderr,
                 "controller_app: cannot apply Key Split: HID unavailable\n");
    return false;
  }
  if (!hid_device_->WriteReport(report)) {
    std::fprintf(stderr, "controller_app: Key Split HID write failed: %s\n",
                 hid_device_->last_error().c_str());
    return false;
  }
  std::fprintf(stderr, "controller_app: applied 129-byte Key Split report\n");
  return true;
}

void ControllerApp::ConfirmJogSelection() {
  if (current_screen_ == ScreenId::kControllerHome) {
    ActivateAction(static_cast<ActionId>(selected_home_button_));
    return;
  }
  if (current_screen_ == ScreenId::kMidiLog) {
    ReturnToControllerHome();
    return;
  }
  if (current_screen_ == ScreenId::kSettings) {
    switch (selected_settings_item_) {
      case 0:
        ReturnToControllerHome();
        break;
      case 2:
        current_screen_ = ScreenId::kKeySplit;
        edited_key_split_settings_ = key_split_settings_;
        selected_key_split_row_ = -1;
        selected_key_split_column_ = 0;
        selected_key_split_action_ = 0;
        selected_key_split_zones_action_ = 0;
        std::fprintf(stderr, "controller_app: screen -> Key Split\n");
        break;
      case 3:
        current_screen_ = ScreenId::kSetCcPc;
        edited_control_assignments_ = control_assignments_;
        selected_ccpc_row_ = -1;
        selected_ccpc_column_ = 0;
        selected_ccpc_action_ = 0;
        ccpc_status_.clear();
        std::fprintf(stderr, "controller_app: screen -> Set CC\n");
        break;
      case 4:
        current_screen_ = ScreenId::kSeqtrakTrackSelect;
        selected_seqtrak_track_ = -1;
        std::fprintf(stderr, "controller_app: screen -> TRACK SELECT\n");
        break;
      default:
        std::fprintf(stderr,
                     "controller_app: selected Settings item is not yet "
                     "implemented\n");
        break;
    }
    return;
  }
  if (current_screen_ == ScreenId::kSeqtrakTrackSelect) {
    if (selected_seqtrak_track_ < 0) {
      current_screen_ = ScreenId::kSettings;
    } else if (selected_seqtrak_track_ >= 0 && selected_seqtrak_track_ <= 8) {
      awm_track_ = selected_seqtrak_track_;
      awm_page_ = 0;
      current_screen_ = ScreenId::kAwm2Editor;
      std::fprintf(stderr, "controller_app: %s -> AWM2 Synth Editor UI\n",
                   seqtrak::kTracks[awm_track_].name);
    } else if (selected_seqtrak_track_ == 9) {
      current_screen_ = ScreenId::kFmEditor;
      fm_page_ = 0;
      selected_fm_header_action_ = 1;
      fm_status_.clear();
      std::fprintf(stderr, "controller_app: DX -> FM Editor UI v2\n");
    }
    return;
  }
  if (current_screen_ == ScreenId::kAwm2Editor) {
    if (awm_page_ == 0) {
      if (awm_overview_mode_ == AwmOverviewMode::kOverview) {
        auto& assigned = awm_elements_[selected_awm_element_][0][0];
        assigned = assigned == 0 ? 1 : 0;
        if (assigned != 0) {
          selected_awm_category_ =
              awm_element_categories_[selected_awm_element_];
          awm_overview_mode_ = AwmOverviewMode::kCategory;
        }
      } else if (awm_overview_mode_ == AwmOverviewMode::kCategory) {
        awm_element_categories_[selected_awm_element_] =
            selected_awm_category_;
        selected_awm_sound_ = 0;
        awm_overview_mode_ = AwmOverviewMode::kSound;
      } else {
        const auto sounds =
            FilteredSoundPresets(1, selected_awm_category_);
        if (!sounds.empty()) {
          selected_awm_sound_ = std::clamp(
              selected_awm_sound_, 0, static_cast<int>(sounds.size()) - 1);
          const auto& sound = *sounds[selected_awm_sound_];
          awm_element_sound_numbers_[selected_awm_element_] = sound.number;
          awm_element_sound_names_[selected_awm_element_] = sound.name;
          awm_elements_[selected_awm_element_][0][1] =
              static_cast<uint8_t>(sound.number & 0x7F);
        }
        awm_overview_mode_ = AwmOverviewMode::kOverview;
      }
    } else {
      awm_page_ = 0;
      awm_overview_mode_ = AwmOverviewMode::kOverview;
    }
    return;
  }
  if (current_screen_ == ScreenId::kFmEditor) {
    if (fm_page_ != 0 || selected_fm_header_action_ == 0) {
      current_screen_ = ScreenId::kSeqtrakTrackSelect;
      selected_seqtrak_track_ = 9;
    } else {
      SaveFmPatch();
    }
    return;
  }
  if (current_screen_ == ScreenId::kKeySplit) {
    if (selected_key_split_row_ == 0) {
      if (selected_key_split_zones_action_ == 1) {
        LoadDrumKeySplitPreset();
      } else if (selected_key_split_zones_action_ == 2) {
        LoadDrumKitKeySplitPreset();
      }
      return;
    }
    if (selected_key_split_row_ != -1) return;
    if (selected_key_split_action_ == 0) {
      NormalizeKeySplitRanges(edited_key_split_settings_);
      if (!ApplyKeySplitSettings(edited_key_split_settings_)) return;
      key_split_settings_ = edited_key_split_settings_;
      std::fprintf(stderr, "controller_app: Key Split committed -> Settings\n");
    } else {
      edited_key_split_settings_ = key_split_settings_;
      std::fprintf(stderr, "controller_app: Key Split cancelled -> Settings\n");
    }
    current_screen_ = ScreenId::kSettings;
    return;
  }
  if (current_screen_ == ScreenId::kSetCcPc) {
    if (selected_ccpc_row_ != -1) return;
    if (selected_ccpc_action_ == 0) {
      if (!SaveControlAssignments(edited_control_assignments_)) return;
      if (!ApplyControlAssignments(edited_control_assignments_)) {
        return;
      }
      control_assignments_ = edited_control_assignments_;
      std::fprintf(stderr,
                   "controller_app: control assignments applied -> Settings\n");
    } else {
      edited_control_assignments_ = control_assignments_;
      std::fprintf(stderr,
                   "controller_app: control assignments cancelled -> Settings\n");
    }
    current_screen_ = ScreenId::kSettings;
    return;
  }
  if (current_screen_ == ScreenId::kDrumSoundCategory ||
      current_screen_ == ScreenId::kSynthSoundCategory ||
      current_screen_ == ScreenId::kDxSoundCategory ||
      current_screen_ == ScreenId::kSamplerSoundCategory ||
      current_screen_ == ScreenId::kDrumKit) {
    if (current_screen_ == ScreenId::kDrumKit) {
      if (selected_drum_kit_item_ == 0) {
        ReturnToVariation();
      } else if (selected_drum_kit_item_ == 9) {
        track_sound_names_[selected_track_] = "Drum Kit";
        track_sound_categories_[selected_track_] = "Drum Kit";
        track_sound_numbers_[selected_track_] = 0;
        track_sound_kinds_[selected_track_] = 4;
        ReturnToVariation();
      } else if (selected_drum_kit_item_ >= 2 &&
                 selected_drum_kit_item_ <= 8) {
        current_screen_ = ScreenId::kDrumSoundCategory;
        selected_sound_category_item_ = 0;
        selected_drum_kit_part_ = selected_drum_kit_item_ - 2;
        sound_list_for_drum_kit_ = true;
        std::fprintf(stderr,
                     "controller_app: Drum Kit part -> Drum Sound Category\n");
      }
    } else if (selected_sound_category_item_ == 0) {
      ReturnToVariation();
    } else {
      if (current_screen_ == ScreenId::kDrumSoundCategory) {
        selected_sound_kind_ = 0;
      } else if (current_screen_ == ScreenId::kSynthSoundCategory) {
        selected_sound_kind_ = 1;
      } else if (current_screen_ == ScreenId::kDxSoundCategory) {
        selected_sound_kind_ = 2;
      } else {
        selected_sound_kind_ = 3;
      }
      selected_sound_category_index_ = selected_sound_category_item_ - 1;
      selected_sound_list_item_ = -1;
      current_screen_ = ScreenId::kSoundList;
    }
    return;
  }
  if (current_screen_ == ScreenId::kSoundList) {
    if (selected_sound_list_item_ == -1) {
      switch (selected_sound_kind_) {
        case 0:
          current_screen_ = ScreenId::kDrumSoundCategory;
          break;
        case 1:
          current_screen_ = ScreenId::kSynthSoundCategory;
          break;
        case 2:
          current_screen_ = ScreenId::kDxSoundCategory;
          break;
        case 3:
          current_screen_ = ScreenId::kSamplerSoundCategory;
          break;
      }
      selected_sound_category_item_ = selected_sound_category_index_ + 1;
    } else {
      ApplySelectedSound();
    }
    return;
  }
  if (current_screen_ != ScreenId::kSoundSelect) return;

  if (lcd_ui_mode_ == LcdUiMode::kTrackSelect) {
    if (selected_variation_item_ == 0) {
      ReturnToControllerHome();
    } else if (selected_variation_item_ == 12) {
      ApplyAllTrackSounds();
    } else if (selected_track_ == 9 || selected_track_ == 10) {
      OpenSelectedSoundDestination();
    } else {
      lcd_ui_mode_ = LcdUiMode::kTrackTypeSelect;
    }
  } else if (lcd_ui_mode_ == LcdUiMode::kTrackTypeSelect) {
    OpenSelectedSoundDestination();
  }
}

void ControllerApp::OnSequencerNoteEvent(const mk2seq::NoteEvent& event) {
  if (event.track_index < 0 ||
      static_cast<size_t>(event.track_index) >= seqtrak::kTrackCount) {
    return;
  }
  int channel = seqtrak::kTracks[event.track_index].midi_channel;
  if (event.note_on) {
    if (router_) {
      router_->SendToSeqtrak(
          mk2::BuildNoteOn(channel, event.note, event.velocity));
    }
  } else {
    if (router_) router_->SendToSeqtrak(mk2::BuildNoteOff(channel, event.note));
  }
}

}  // namespace mk2app
