// KOMPLETE KONTROL S49 MK2 (and S61/S88 MK2) USB/HID protocol constants.
//
// Reverse-engineered by the KompleteControl_MK2 Python project (see 資料/).
// Ported to C++ for the Raspberry Pi Zero 2 W standalone controller/sequencer.
//
// Sources referenced while porting: s49mk2/protocol.py, device.py, display.py,
// buttonled.py, lightguide.py, controls.py, keyzones.py, hid_transport.py,
// usb_transport.py, and protocol.md ("confirmed" section).
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace mk2 {

// ---------------------------------------------------------------------------
// USB identification
// ---------------------------------------------------------------------------

constexpr uint16_t kNiVendorId = 0x17CC;

constexpr uint16_t kProductIdS49Mk2 = 0x1610;
constexpr uint16_t kProductIdS61Mk2 = 0x1620;
constexpr uint16_t kProductIdS88Mk2 = 0x1630;

constexpr std::array<uint16_t, 3> kSupportedProductIds = {
    kProductIdS49Mk2, kProductIdS61Mk2, kProductIdS88Mk2};

struct ProductInfo {
  uint16_t product_id;
  const char* name;
  int key_count;
  int lowest_midi_note;
};

constexpr ProductInfo kProducts[] = {
    {kProductIdS49Mk2, "KOMPLETE KONTROL S49 MK2", 49, 36},
    {kProductIdS61Mk2, "KOMPLETE KONTROL S61 MK2", 61, 36},
    {kProductIdS88Mk2, "KOMPLETE KONTROL S88 MK2", 88, 21},
};

// ---------------------------------------------------------------------------
// USB descriptor layout (confirmed via PyUSB/libusb descriptor enumeration on
// a real S49 MK2; interface numbers and endpoint addresses are descriptor-
// driven at runtime, these are the expected/preferred values):
//
//   interface 0: audio class, no endpoints
//   interface 1: audio class, bulk IN 0x81, bulk OUT 0x01
//   interface 2: HID class, interrupt IN 0x82, interrupt OUT 0x02
//   interface 3: vendor class 0xff, bulk OUT 0x03, max packet size 512
//   interface 4: class 0xfe, no endpoints
//
// The LCD screens are driven over the vendor-class bulk OUT endpoint on
// interface 3. Knobs/buttons/jog/light-guide/LED writes go over the HID
// interface (interface 2), via a generic HID report reader/writer (hidraw).
// ---------------------------------------------------------------------------

constexpr int kLcdBulkInterfaceNumber = 3;
constexpr uint8_t kLcdBulkOutEndpoint = 0x03;
constexpr int kLcdBulkMaxPacketSize = 512;

constexpr int kHidInterfaceNumber = 2;
constexpr uint8_t kHidInterruptInEndpoint = 0x82;
constexpr uint8_t kHidInterruptOutEndpoint = 0x02;

// ---------------------------------------------------------------------------
// LCD screen bulk-write protocol
// ---------------------------------------------------------------------------
//
// Packet layout (24-byte header + width*height*2 RGB565 pixel bytes + 12-byte
// suffix):
//
//   offset  size  field
//   0       2     prefix 84 00
//   2       1     screen index (0 = left, 1 = right)
//   3       5     constant 60 00 00 00 00
//   8       2     x            (big-endian u16)
//   10      2     y            (big-endian u16)
//   12      2     width        (big-endian u16)
//   14      2     height       (big-endian u16)
//   16      6     constant 02 00 00 00 00 00
//   22      2     pixel data length in 32-bit words (big-endian u16)
//                 = ceil(width*height*2 / 4) = width*height/2 for full frames
//   24      N     RGB565 pixel data, big-endian per pixel (confirmed for red:
//                 0xf8 0x00)
//   24+N    12    suffix 02 00 00 00 03 00 00 00 40 00 00 00
//
// Screens are 480x272 each; the S49/S61/S88 MK2 has two side-by-side LCDs.

constexpr int kLcdWidth = 480;
constexpr int kLcdHeight = 272;

constexpr int kLcdScreenLeft = 0;
constexpr int kLcdScreenRight = 1;

constexpr std::array<uint8_t, 2> kLcdPacketPrefix = {0x84, 0x00};
constexpr std::array<uint8_t, 5> kLcdHeaderConstantA = {0x60, 0x00, 0x00,
                                                         0x00, 0x00};
constexpr std::array<uint8_t, 6> kLcdHeaderConstantB = {0x02, 0x00, 0x00,
                                                          0x00, 0x00, 0x00};
constexpr std::array<uint8_t, 12> kLcdPacketSuffix = {
    0x02, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00};

// 24-byte header + 12-byte suffix, pixel payload sized separately.
constexpr size_t kLcdHeaderSize = 24;
constexpr size_t kLcdSuffixSize = 12;

// Pixel format: RGB565, packed big-endian per 16-bit pixel.
// rgb565 = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
inline uint16_t RgbToRgb565(uint8_t r, uint8_t g, uint8_t b) {
  return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) |
                                (b >> 3));
}

// ---------------------------------------------------------------------------
// HID report IDs (byte 0 of each report)
// ---------------------------------------------------------------------------

// Input report emitted continuously by the controller (knobs, buttons, jog).
constexpr uint8_t kHidReportInput = 0x01;

// Light Guide (49/61/88-key LED strip) update. Payload is 249 bytes, one
// per key-guide LED slot, each a palette-index color value.
constexpr uint8_t kHidReportLightGuide = 0x81;
constexpr int kLightGuidePayloadLen = 249;
// Sent once before Light Guide writes to initialize the LED controller.
constexpr std::array<uint8_t, 3> kHidReportLightGuideInit = {0xA0, 0x00,
                                                               0x00};

// Panel + LCD-adjacent Function button LED update. 80-byte report:
// byte 0 = 0x80, bytes 1..79 = per-button-ID color value (index = button ID).
constexpr uint8_t kHidReportButtonLed = 0x80;
constexpr int kButtonLedReportLen = 80;

// Keyzone (split/layer) configuration. 129-byte report:
// byte 0 = 0xA4, 16 zone entries x 8 bytes each.
constexpr uint8_t kHidReportKeyzones = 0xA4;
constexpr int kKeyzoneCount = 16;
constexpr int kKeyzoneEntryLen = 8;

// LCD-adjacent knob/button MIDI CC assignment. 204-byte report:
// byte 0 = 0xA1, 8x12-byte button entries, 8x12-byte knob entries,
// 8 button-background-color bytes, 3-byte fixed suffix (00 00 00).
constexpr uint8_t kHidReportControlsAssign = 0xA1;

// Pitch/mod wheel + touch strip assignment. 45-byte report:
// byte 0 = 0xA2, 3x12-byte entries (pitch, mod, touch strip),
// 4-byte touch-strip pitch range data, 4-byte fixed suffix.
constexpr uint8_t kHidReportSlidersAssign = 0xA2;

// Pedal assignment. 73-byte report:
// byte 0 = 0xA3, 2x12-byte continuous entries, 4x12-byte switch entries.
constexpr uint8_t kHidReportPedalsAssign = 0xA3;
// Two 33-byte pedal *port* declaration reports sent before 0xA3:
// prefix f4 22, then [port_select, 0x03, port_type], then 28 zero bytes.
// port_select: 0x01 = pedal port 1, 0x00 = pedal port 2.
constexpr std::array<uint8_t, 2> kHidReportPedalPortPrefix = {0xF4, 0x22};
constexpr int kPedalPortReportLen = 33;

// qKontrol-observed alternate knob input report (unused by this port; we
// decode knobs from kHidReportInput instead, see below).
constexpr uint8_t kHidReportKnobAlt = 0xAA;
constexpr int kHidReportKnobAltLen = 51;

// ---------------------------------------------------------------------------
// HID input report 0x01 layout (locally confirmed on real S49 MK2 hardware)
// ---------------------------------------------------------------------------

// Function buttons 1..8 (the eight buttons flanking the LCDs): byte 1,
// one bit per button -- per the upstream Python project's protocol.md.
//
// CORRECTION (real hardware, Pi 4 + Linux, this project): on the unit
// tested, byte 1 was observed to NEVER change while repeatedly pressing
// all eight of these buttons (confirmed with a raw, table-free byte dump --
// not a decode bug). These buttons did not reach the host via HID report
// 0x01 at all. Instead they showed up as native Control Change messages
// sent directly over the class-compliant USB-MIDI interface: one press per
// button, channel 1, CC 0x70..0x77 (112..119 decimal, sequential).
//
// This is NOT a fixed protocol fact the way the LCD packet header is --
// unlike that, CC assignment is reconfigurable (via the 0xA1 HID report;
// see kDefaultKnobCcBase/kDefaultFunctionButtonCcBase below, which is what
// the upstream Python project's qKontrol-derived 0xA1 write reassigns these
// buttons to: CC22..29). CC 0x70..0x77 is simply whatever this unit
// happened to be sending at observation time -- most likely the
// firmware/factory default that applies before any host has ever written
// a custom 0xA1 assignment, but that has not been independently confirmed
// (e.g. it could instead reflect a prior 0xA1 write from some other host
// this unit was once connected to). Treat kObservedFunctionButtonCcBase
// below as "what we saw on one unit, once," not as a protocol constant.
//
// Net effect: do not rely on kInputByteFunctionButtons/kFunctionButtonMasks
// for detecting these presses -- read them from the MIDI stream instead
// (they pass through Mk2SeqtrakRouter's raw relay unmodified already, no
// extra code needed). The constants below are kept for reference / in case
// a future unit or firmware revision reports through HID instead.
constexpr int kInputByteFunctionButtons = 1;
constexpr std::array<uint8_t, 8> kFunctionButtonMasks = {
    0x10, 0x20, 0x40, 0x80, 0x01, 0x02, 0x04, 0x08};

// CC observed for Function1..8 on one real unit, in whatever assignment
// state it happened to be in at the time (see correction note above). Not
// guaranteed to match any other unit or to survive a future 0xA1 write.
// 0x70..0x77 hex == 112..119 decimal, CC = base + index, channel 1.
constexpr int kObservedFunctionButtonCcBase = 0x70;  // == 112

// Eight LCD knobs: byte 7 holds touch/active bits (bit i = knob i+1 touched);
// bytes 10..25 hold eight little-endian u16 values, one knob per 2 bytes,
// each cycling through a 0..999 range (wraps).
constexpr int kInputByteKnobTouch = 7;
constexpr std::array<uint8_t, 8> kKnobTouchMasks = {0x80, 0x40, 0x20, 0x10,
                                                     0x08, 0x04, 0x02, 0x01};
constexpr int kInputKnobValueBase = 10;  // bytes[10..11] = knob 1, etc.
constexpr int kKnobCount = 8;
constexpr int kKnobValueModulo = 1000;

// Jog wheel: byte 6 = touch/press/direction control byte, byte 30 = 4-bit
// rotation counter (wraps 0x0..0xF).
constexpr int kInputByteJogControl = 6;
constexpr int kInputByteJogTurn = 30;
constexpr uint8_t kJogIdle = 0x00;
constexpr uint8_t kJogTouch = 0x04;
constexpr uint8_t kJogPress = 0x0C;
constexpr uint8_t kJogLeft = 0x14;
constexpr uint8_t kJogUp = 0x24;
constexpr uint8_t kJogDown = 0x44;
constexpr uint8_t kJogRight = 0x84;

// Panel button input events: (report byte index, mask/exact value, LED id,
// name). `exact` events require report[byte] == value; `mask` events require
// (report[byte] & value) == value (used only for the Function-row buttons,
// which multiplex several bits on the same byte).
enum class ButtonMatch { kExact, kMask };

struct ButtonInputEvent {
  int byte_index;
  uint8_t value;
  int led_id;
  const char* name;
  ButtonMatch match;
};

// Button LED IDs. LCD Function buttons are IDs 2..9 (report byte = id + 1 in
// the 0x80 LED report). Panel buttons occupy IDs 0..13 and 15..41. IDs 14,
// 42, 43 and 44..68 are confirmed-unused-or-unmapped LED slots (44+ suspected
// touch-strip/segment indicators, never confirmed).
constexpr std::array<int, 8> kLcdFunctionButtonLedIds = {2, 3, 4, 5,
                                                          6, 7, 8, 9};

constexpr ButtonInputEvent kKnownButtonInputEvents[] = {
    {1, 0x10, 2, "Function1", ButtonMatch::kMask},
    {1, 0x20, 3, "Function2", ButtonMatch::kMask},
    {1, 0x40, 4, "Function3", ButtonMatch::kMask},
    {1, 0x80, 5, "Function4", ButtonMatch::kMask},
    {1, 0x01, 6, "Function5", ButtonMatch::kMask},
    {1, 0x02, 7, "Function6", ButtonMatch::kMask},
    {1, 0x04, 8, "Function7", ButtonMatch::kMask},
    {1, 0x08, 9, "Function8", ButtonMatch::kMask},
    {2, 0x80, 14, "Shift", ButtonMatch::kExact},
    {2, 0x08, 15, "ScaleEdit", ButtonMatch::kExact},
    {2, 0x04, 16, "ArpEdit", ButtonMatch::kExact},
    {2, 0x10, 29, "Play", ButtonMatch::kExact},
    {2, 0x20, 24, "Loop", ButtonMatch::kExact},
    {2, 0x40, 18, "UndoRedo", ButtonMatch::kExact},
    {2, 0x02, 19, "Quantize", ButtonMatch::kExact},
    {2, 0x01, 20, "Auto", ButtonMatch::kExact},
    {3, 0x02, 30, "Record", ButtonMatch::kExact},
    {3, 0x01, 31, "Stop", ButtonMatch::kExact},
    {3, 0x04, 26, "Tempo", ButtonMatch::kExact},
    {3, 0x08, 25, "Metro", ButtonMatch::kExact},
    {3, 0x10, 22, "PresetUp", ButtonMatch::kExact},
    {3, 0x40, 27, "PresetDown", ButtonMatch::kExact},
    {3, 0x80, 32, "PageLeft", ButtonMatch::kExact},
    {3, 0x20, 33, "PageRight", ButtonMatch::kExact},
    {4, 0x01, 0, "M", ButtonMatch::kExact},
    {4, 0x02, 1, "S", ButtonMatch::kExact},
    {4, 0x04, 17, "Scene", ButtonMatch::kExact},
    {4, 0x08, 21, "Pattern", ButtonMatch::kExact},
    {4, 0x10, 23, "Track", ButtonMatch::kExact},
    {4, 0x20, 34, "Clear", ButtonMatch::kExact},
    {4, 0x40, 28, "KeyMode", ButtonMatch::kExact},
    {5, 0x01, 37, "Mixer", ButtonMatch::kExact},
    {5, 0x02, 36, "Plugin", ButtonMatch::kExact},
    {5, 0x04, 35, "Browser", ButtonMatch::kExact},
    {5, 0x08, 40, "Setup", ButtonMatch::kExact},
    {5, 0x10, 38, "Instance", ButtonMatch::kExact},
    {5, 0x20, 39, "MIDI", ButtonMatch::kExact},
    {6, 0x14, 10, "JogLeft", ButtonMatch::kExact},
    {6, 0x24, 11, "JogUp", ButtonMatch::kExact},
    {6, 0x44, 12, "JogDown", ButtonMatch::kExact},
    {6, 0x84, 13, "JogRight", ButtonMatch::kExact},
    {8, 0x04, 41, "FixedVel", ButtonMatch::kExact},
};
constexpr size_t kKnownButtonInputEventCount =
    sizeof(kKnownButtonInputEvents) / sizeof(kKnownButtonInputEvents[0]);

// ---------------------------------------------------------------------------
// Light Guide color palette (0x81 report payload byte values)
// ---------------------------------------------------------------------------

constexpr uint8_t kLightGuideOff = 0x00;
constexpr uint8_t kLightGuideRed = 0x04;
constexpr uint8_t kLightGuideOrange = 0x08;
constexpr uint8_t kLightGuideYellow = 0x14;
constexpr uint8_t kLightGuideGreen = 0x1C;
constexpr uint8_t kLightGuideMint = 0x20;
constexpr uint8_t kLightGuideCyan = 0x24;
constexpr uint8_t kLightGuideBlue = 0x2C;
constexpr uint8_t kLightGuidePurple = 0x38;
constexpr uint8_t kLightGuideWhite = 0x1F;

// ---------------------------------------------------------------------------
// 0xA1 control-assignment report field layout (inferred from qKontrol)
// ---------------------------------------------------------------------------
//
// Button assignment entry (12 bytes), one of 8, for the LCD Function buttons:
//   0  mode: 0x00 off, 0x03 MIDI/CC, 0x04 plugin
//   1  CC number
//   2  MIDI channel, zero-based
//   3  action: 0x3C toggle, 0x3D trigger/other, 0x3E gate
//   4-5 reserved 00 00
//   6  on value (or CC number in plugin mode)
//   7-11 reserved 00 00 00 00 00
//
// Knob assignment entry (12 bytes), one of 8:
//   0  mode: 0x00 off, 0x03 MIDI/CC, 0x04 plugin
//   1  CC number
//   2  MIDI channel, zero-based
//   3-11 fixed bytes 3c 00 00 7f 00 00 00 00 00
//
// Report tail: 8 button-background-color bytes, then fixed suffix 00 00 00.

constexpr uint8_t kControlModeOff = 0x00;
constexpr uint8_t kControlModeMidiCc = 0x03;
constexpr uint8_t kControlModePlugin = 0x04;

constexpr uint8_t kButtonActionToggle = 0x3C;
constexpr uint8_t kButtonActionTrigger = 0x3D;
constexpr uint8_t kButtonActionGate = 0x3E;

constexpr std::array<uint8_t, 9> kKnobAssignFixedTail = {
    0x3C, 0x00, 0x00, 0x7F, 0x00, 0x00, 0x00, 0x00, 0x00};

// Suggested/default CC assignment used by this project's HID writer when it
// programs the 0xA1 report (matches the Python tool's `controls-a1` default
// and qKontrol's factory-preset behavior): Knob1..8 -> CC14..21,
// Function1..8 -> CC22..29, MIDI channel 1.
constexpr int kDefaultKnobCcBase = 14;
constexpr int kDefaultFunctionButtonCcBase = 22;
constexpr int kDefaultControlsMidiChannel = 1;  // 1-based

// ---------------------------------------------------------------------------
// 0xA2 slider/wheel assignment report field layout
// ---------------------------------------------------------------------------
//
// Three 12-byte entries in order: pitch wheel, mod wheel, touch strip.
//   off:   12 zero bytes
//   CC:    03 <cc> <channel-0> 20 <lower> 00 <upper> 00 00 00 00 00
//   pitch: 06 00 <channel-0> 00 00 00 ff 3f 00 00 01 00
// Bytes 37..40 hold touch-strip pitch-range/zero-point data when the touch
// strip is assigned to pitch mode, else 00 00 00 00. Suffix: 00 00 00 00.

constexpr uint8_t kSliderModeOff = 0x00;
constexpr uint8_t kSliderModeCc = 0x03;
constexpr uint8_t kSliderModePitch = 0x06;

// ---------------------------------------------------------------------------
// 0xA3 pedal assignment report field layout
// ---------------------------------------------------------------------------
//
// Continuous pedal entry (12 bytes), pedal 1 then pedal 2:
//   0  mode: 0x00 off, 0x03 MIDI/CC, 0x04 plugin
//   1  CC number
//   2  MIDI channel, zero-based
//   3  port marker: pedal1 0x94, pedal2 0x00
//   4  lower value
//   5  0x00
//   6  upper value
//   7  0x00
//   8-11 port-specific suffix: pedal1 60 50 f1 ac, pedal2 c0 60 00 00
//
// Switch entry (12 bytes), pedal1-tip, pedal1-ring, pedal2-tip, pedal2-ring:
//   0  mode: 0x00 off, 0x03 MIDI/CC, 0x04 plugin
//   1  CC number
//   2  MIDI channel, zero-based
//   3  action: 0x36 gate, 0x37 increment, 0x3F increment-wrap, 0x35 trigger,
//      0x34 toggle
//   4  off value
//   5  0x00
//   6  on value
//   7-9 00 00 00
//   10 step (increment mode only, else 0x00)
//   11 0x00

constexpr uint8_t kPedalPortTypeContinuous = 0x02;
constexpr uint8_t kPedalPortTypeSwitch = 0x03;
constexpr uint8_t kPedalPortTypeContinuousSwap = 0x01;
constexpr uint8_t kPedalPortTypeContinuousInvert = 0x06;
constexpr uint8_t kPedalPortTypeContinuousSwapInvert = 0x05;

constexpr uint8_t kPedalSwitchActionGate = 0x36;
constexpr uint8_t kPedalSwitchActionIncrement = 0x37;
constexpr uint8_t kPedalSwitchActionIncrementWrap = 0x3F;
constexpr uint8_t kPedalSwitchActionTrigger = 0x35;
constexpr uint8_t kPedalSwitchActionToggle = 0x34;

constexpr std::array<uint8_t, 4> kPedal1ContinuousSuffix = {0x60, 0x50, 0xF1,
                                                             0xAC};
constexpr std::array<uint8_t, 4> kPedal2ContinuousSuffix = {0xC0, 0x60, 0x00,
                                                             0x00};
constexpr uint8_t kPedal1ContinuousMarker = 0x94;
constexpr uint8_t kPedal2ContinuousMarker = 0x00;

// ---------------------------------------------------------------------------
// 0xA4 keyzone report field layout
// ---------------------------------------------------------------------------
//
// 16 zone entries, 8 bytes each, following the 1-byte report ID:
//   0  zone end key / boundary (post-octave-shift MIDI note number)
//   1  transpose, signed int8 (unconfirmed sign convention in all firmware)
//   2  MIDI channel, zero-based (0..15)
//   3  velocity curve, or 0x83 = zone off
//   4-5 Light Guide zone color (palette bytes, see kZoneColor* below)
//   6-7 reserved 00 00
//
// Unused zone slots should be padded with key=127, velocity=off, color=off.

constexpr uint8_t kVelocityCurveSoft3 = 0x30;
constexpr uint8_t kVelocityCurveSoft2 = 0x31;
constexpr uint8_t kVelocityCurveSoft1 = 0x32;
constexpr uint8_t kVelocityCurveLinear = 0x33;
constexpr uint8_t kVelocityCurveHard1 = 0x34;
constexpr uint8_t kVelocityCurveHard2 = 0x35;
constexpr uint8_t kVelocityCurveHard3 = 0x36;
constexpr uint8_t kVelocityCurveZoneOff = 0x83;

// Zone color bytes (2-byte pairs) seen in qKontrol.
constexpr std::array<uint8_t, 2> kZoneColorBlue = {0x2C, 0x2E};
constexpr std::array<uint8_t, 2> kZoneColorRed = {0x04, 0x06};
constexpr std::array<uint8_t, 2> kZoneColorOrange = {0x08, 0x0A};
constexpr std::array<uint8_t, 2> kZoneColorGreen = {0x1C, 0x1E};
constexpr std::array<uint8_t, 2> kZoneColorYellow = {0x14, 0x16};
constexpr std::array<uint8_t, 2> kZoneColorMint = {0x20, 0x22};
constexpr std::array<uint8_t, 2> kZoneColorPurple = {0x38, 0x3A};
constexpr std::array<uint8_t, 2> kZoneColorCyan = {0x24, 0x26};
constexpr std::array<uint8_t, 2> kZoneColorOff = {0x00, 0x00};

}  // namespace mk2
