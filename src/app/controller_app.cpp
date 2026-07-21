#include "app/controller_app.h"

#include <cstdio>

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

bool EventActive(const std::vector<uint8_t>& report,
                  const mk2::ButtonInputEvent& event) {
  if (static_cast<size_t>(event.byte_index) >= report.size()) return false;
  uint8_t value = report[event.byte_index];
  if (event.match == mk2::ButtonMatch::kMask) {
    return (value & event.value) == event.value;
  }
  return value == event.value;
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
    std::fprintf(stderr, "controller_app: MK2 MIDI port open failed: %s\n",
                 mk2_midi_port_->last_error().c_str());
    return false;
  }

  seqtrak_midi_port_ = std::make_unique<mk2::AlsaRawMidiPort>();
  if (!seqtrak_midi_port_->OpenByNameSubstring("SEQTRAK")) {
    std::fprintf(stderr,
                 "controller_app: SEQTRAK MIDI port open failed: %s\n",
                 seqtrak_midi_port_->last_error().c_str());
    return false;
  }

  router_ = std::make_unique<mk2::Mk2SeqtrakRouter>(
      mk2_midi_port_.get(), seqtrak_midi_port_.get(), dry_run_);

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

  mk2::LcdCanvas left;
  left.Clear(0, 0, 0);
  left.FillRect(16, 16, mk2::kLcdWidth - 32, mk2::kLcdHeight - 32, 0, 40, 0);
  left.DrawRect(0, 0, mk2::kLcdWidth, mk2::kLcdHeight, 0, 255, 0);
  SendOrPreviewLcdPacket(lcd_device_.get(), dry_run_, "left",
                         mk2::BuildLcdPacket(mk2::kLcdScreenLeft, left));

  mk2::LcdCanvas right;
  right.Clear(0, 0, 0);
  right.FillRect(16, 16, mk2::kLcdWidth - 32, mk2::kLcdHeight - 32, 0, 0, 40);
  right.DrawRect(0, 0, mk2::kLcdWidth, mk2::kLcdHeight, 0, 128, 255);
  SendOrPreviewLcdPacket(lcd_device_.get(), dry_run_, "right",
                         mk2::BuildLcdPacket(mk2::kLcdScreenRight, right));
}

void ControllerApp::Run() {
  running_.store(true);
  DrawStartupScreens();
  router_->Start();
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
    if (!report.has_value()) continue;
    HandleHidReport(*report);
    previous_hid_report_ = *report;
  }
}

void ControllerApp::HandleHidReport(const std::vector<uint8_t>& report) {
  if (report.empty() || report[0] != mk2::kHidReportInput) return;

  // Function buttons 1..8 -> CC22..29 (press = value 127, per SEQTRAK CC
  // table's expectation of "value 127 for the default toggle/action test").
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
      uint8_t cc = static_cast<uint8_t>(
          mk2::kDefaultFunctionButtonCcBase + static_cast<int>(i));
      router_->SendToSeqtrak(
          mk2::BuildControlChange(kControlChannel, cc, 127));
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

      uint8_t cc =
          static_cast<uint8_t>(mk2::kDefaultKnobCcBase + knob);
      uint8_t cc_value = static_cast<uint8_t>(
          (static_cast<uint32_t>(cur_value) * 127) / (mk2::kKnobValueModulo - 1));
      router_->SendToSeqtrak(
          mk2::BuildControlChange(kControlChannel, cc, cc_value));
    }
  }
}

void ControllerApp::OnSequencerNoteEvent(const mk2seq::NoteEvent& event) {
  if (event.track_index < 0 ||
      static_cast<size_t>(event.track_index) >= seqtrak::kTrackCount) {
    return;
  }
  int channel = seqtrak::kTracks[event.track_index].midi_channel;
  if (event.note_on) {
    router_->SendToSeqtrak(
        mk2::BuildNoteOn(channel, event.note, event.velocity));
  } else {
    router_->SendToSeqtrak(mk2::BuildNoteOff(channel, event.note));
  }
}

}  // namespace mk2app
