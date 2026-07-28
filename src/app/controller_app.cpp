#include "app/controller_app.h"

#include <algorithm>
#include <cstdio>
#include <string>

#include "display/lcd_packet.h"
#include "mk2_protocol.h"
#include "seqtrak_protocol.h"
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
constexpr const char* kTrackTypeNames[] = {"Drum", "DrumKit", "Synth"};
constexpr int kVolumeMin = 0;
constexpr int kVolumeMax = 100;
constexpr int kPanMin = -50;
constexpr int kPanMax = 50;
constexpr auto kKnobDoubleTouchMin = std::chrono::milliseconds(80);
constexpr auto kKnobDoubleTouchMax = std::chrono::milliseconds(500);
constexpr size_t kMidiLogCapacity = 11;

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
      "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
  if (note < 0 || note > 127) return std::to_string(note);
  return std::string(kNames[note % 12]) + std::to_string(note / 12 - 2);
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

ControllerApp::~ControllerApp() { Stop(); }

bool ControllerApp::Initialize() {
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
  if (current_screen_ == ScreenId::kKeySplit) {
    DrawKeySplit(left);
    SendOrPreviewLcdPacket(lcd_device_.get(), dry_run_, "left",
                           mk2::BuildLcdPacket(mk2::kLcdScreenLeft, left));
    return;
  }
  if (current_screen_ == ScreenId::kSetCcPc) {
    DrawSetCcPc(left);
    SendOrPreviewLcdPacket(lcd_device_.get(), dry_run_, "left",
                           mk2::BuildLcdPacket(mk2::kLcdScreenLeft, left));
    return;
  }

  constexpr int kGap = 6;
  constexpr int kButtonHeight = 32;
  constexpr int kPaddingX = 12;
  constexpr int kRowY[] = {10, 50};
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
      bool selected = i == selected_track_;
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

    const int volume = track_volumes_[selected_track_];
    DrawShinonomeText(left, 12, 157, "Volume", 244, 247, 250);
    DrawShinonomeText(left, 96, 157, "0", 170, 179, 192);
    FillRoundedRect(left, 112, 160, 300, 10, 5, 37, 43, 52);
    FillRoundedRect(left, 112, 160, 3 * volume, 10, 5, 79, 195, 247);
    int volume_x = 112 + 3 * volume;
    FillRoundedRect(left, volume_x - 4, 155, 8, 20, 3, 234, 248, 255);
    DrawShinonomeText(left, 420, 157, "100", 170, 179, 192);
    DrawShinonomeText(left, std::clamp(volume_x - 12, 112, 388), 136,
                      std::to_string(volume) + "%", 79, 195, 247);

    const int pan = track_pans_[selected_track_];
    DrawShinonomeText(left, 12, 213, "Pan", 244, 247, 250);
    DrawShinonomeText(left, 96, 213, "L", 170, 179, 192);
    FillRoundedRect(left, 112, 216, 300, 10, 5, 37, 43, 52);
    int pan_x = 262 + (pan * 150) / kPanMax;
    left.FillRect(260, 216, 4, 10, 79, 195, 247);
    if (pan < 0) {
      left.FillRect(pan_x, 216, 262 - pan_x, 10, 79, 195, 247);
    } else if (pan > 0) {
      left.FillRect(262, 216, pan_x - 262, 10, 79, 195, 247);
    }
    FillRoundedRect(left, pan_x - 6, 211, 12, 20, 4, 234, 248, 255);
    DrawShinonomeText(left, 424, 213, "R", 170, 179, 192);
    const std::string pan_text =
        pan > 0 ? "+" + std::to_string(pan) : std::to_string(pan);
    DrawShinonomeText(left, std::clamp(pan_x - 12, 112, 388), 192,
                      pan_text, 79, 195, 247);
  }

  SendOrPreviewLcdPacket(lcd_device_.get(), dry_run_, "left",
                         mk2::BuildLcdPacket(mk2::kLcdScreenLeft, left));
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
      "S49MK2", "Key Split", "Set CC/PC", "SERTRAK", "Controller"};
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

void ControllerApp::DrawKeySplit(mk2::LcdCanvas& canvas) {
  canvas.FillRect(0, 0, mk2::kLcdWidth, 28, 27, 32, 40);
  DrawShinonomeText(canvas, 12, 6, "Key Split", 244, 247, 250);
  constexpr const char* kActions[] = {"OK", "Cancel"};
  constexpr int kActionX[] = {340, 388};
  constexpr int kActionWidth[] = {40, 76};
  for (int i = 0; i < 2; ++i) {
    const bool selected = selected_dialog_action_ == i;
    FillRoundedRect(canvas, kActionX[i], 4, kActionWidth[i], 20, 3,
                    selected ? 0 : 32, selected ? 215 : 42,
                    selected ? 255 : 51);
    DrawShinonomeText(canvas, kActionX[i] + 8, 6, kActions[i],
                      selected ? 6 : 170, selected ? 16 : 179,
                      selected ? 20 : 192);
  }

  DrawShinonomeText(canvas, 12, 38, "Zones: 1", 244, 247, 250);
  DrawShinonomeText(canvas, 12, 58, "Zone01:C2-G8 CH:1 Trans:C2", 244, 247,
                    250);
  canvas.FillRect(326, 59, 12, 12, 79, 195, 247);
  for (int zone = 2; zone <= 11; ++zone) {
    char line[64];
    std::snprintf(line, sizeof(line), "Zone%02d: -- disabled --", zone);
    DrawShinonomeText(canvas, 12, 58 + (zone - 1) * 18, line, 82, 97, 109);
  }
}

void ControllerApp::DrawSetCcPc(mk2::LcdCanvas& canvas) {
  canvas.FillRect(0, 0, mk2::kLcdWidth, 28, 27, 32, 40);
  DrawShinonomeText(canvas, 12, 6, "Page 1", 244, 247, 250);
  constexpr const char* kActions[] = {"OK", "Cancel"};
  constexpr int kActionX[] = {340, 388};
  constexpr int kActionWidth[] = {40, 76};
  for (int i = 0; i < 2; ++i) {
    const bool selected = selected_dialog_action_ == i;
    FillRoundedRect(canvas, kActionX[i], 4, kActionWidth[i], 20, 3,
                    selected ? 0 : 32, selected ? 215 : 42,
                    selected ? 255 : 51);
    DrawShinonomeText(canvas, kActionX[i] + 8, 6, kActions[i],
                      selected ? 6 : 170, selected ? 16 : 179,
                      selected ? 20 : 192);
  }

  DrawShinonomeText(canvas, 12, 38, "CONTROL       TYPE NUM", 170, 179, 192);
  DrawShinonomeText(canvas, 252, 38, "CONTROL      TYPE NUM", 170, 179, 192);
  for (int row = 0; row < 10; ++row) {
    char left[48];
    char right[48];
    if (row < 8) {
      std::snprintf(left, sizeof(left), "Knob %d       CC  000", row + 1);
      std::snprintf(right, sizeof(right), "Button %d    CC  000", row + 1);
    } else {
      std::snprintf(left, sizeof(left), "Pedal %c      CC  000",
                    row == 8 ? 'A' : 'B');
      if (row == 8) {
        std::snprintf(right, sizeof(right), "Touch Strip CC  000");
      } else {
        right[0] = '\0';
      }
    }
    const int y = 58 + row * 20;
    DrawShinonomeText(canvas, 12, y, left, 244, 247, 250);
    if (right[0] != '\0') {
      DrawShinonomeText(canvas, 252, y, right, 244, 247, 250);
    }
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
    if (knob >= 0 && knob < 2) {
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
  if (current_screen_ != ScreenId::kSoundSelect ||
      lcd_ui_mode_ == LcdUiMode::kTrackSelect) {
    pending_knob_cc_[0].store(-1);
    pending_knob_cc_[1].store(-1);
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
      MoveJogSelection(delta);
      ui_changed = true;
    }
  }

  if (report.size() > static_cast<size_t>(mk2::kInputByteJogControl)) {
    uint8_t current = current_jog_control;
    uint8_t previous = previous_jog_control;
    if (current != previous) {
      if (current == mk2::kJogLeft) {
        MoveJogSelection(-1);
        ui_changed = true;
      } else if (current == mk2::kJogRight) {
        MoveJogSelection(1);
        ui_changed = true;
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

      // In the track settings screens, LCD knob 1 edits Volume (0..100) and
      // knob 2 edits Pan (-50..50). Positive hardware deltas are clockwise;
      // negative deltas are counter-clockwise.
      if (current_screen_ == ScreenId::kSoundSelect &&
          lcd_ui_mode_ != LcdUiMode::kTrackSelect && physical_knob < 2 &&
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
  if (current_screen_ == ScreenId::kKeySplit ||
      current_screen_ == ScreenId::kSetCcPc) {
    constexpr int count = 2;
    selected_dialog_action_ =
        (selected_dialog_action_ + delta % count + count) % count;
    return;
  }
  if (current_screen_ != ScreenId::kSoundSelect) return;

  if (lcd_ui_mode_ == LcdUiMode::kTrackSelect) {
    int count = static_cast<int>(seqtrak::kTrackCount);
    selected_track_ = (selected_track_ + delta % count + count) % count;
  } else if (lcd_ui_mode_ == LcdUiMode::kTrackTypeSelect) {
    constexpr int count = 3;
    int& type = track_types_[selected_track_];
    type = (type + delta % count + count) % count;
  }
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
        selected_dialog_action_ = 0;
        std::fprintf(stderr, "controller_app: screen -> Key Split\n");
        break;
      case 3:
        current_screen_ = ScreenId::kSetCcPc;
        selected_dialog_action_ = 0;
        std::fprintf(stderr, "controller_app: screen -> Set CC/PC\n");
        break;
      default:
        std::fprintf(stderr,
                     "controller_app: selected Settings item is not yet "
                     "implemented\n");
        break;
    }
    return;
  }
  if (current_screen_ == ScreenId::kKeySplit ||
      current_screen_ == ScreenId::kSetCcPc) {
    std::fprintf(stderr, "controller_app: %s -> Settings\n",
                 selected_dialog_action_ == 0 ? "OK" : "Cancel");
    current_screen_ = ScreenId::kSettings;
    return;
  }
  if (current_screen_ != ScreenId::kSoundSelect) return;

  if (lcd_ui_mode_ == LcdUiMode::kTrackSelect) {
    lcd_ui_mode_ = (selected_track_ == 9 || selected_track_ == 10)
                       ? LcdUiMode::kTrackDetail
                       : LcdUiMode::kTrackTypeSelect;
  } else if (lcd_ui_mode_ == LcdUiMode::kTrackTypeSelect) {
    lcd_ui_mode_ = LcdUiMode::kTrackDetail;
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
