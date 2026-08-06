// YAMAHA SEQTRAK MIDI implementation constants.
//
// Extracted from 資料/SEQTRAK_data_list_En_D0.pdf ("MIDI Data Format" and
// "MIDI Data Table" sections, OS V2.00 / Data List revision YJ-D0).
//
// This header intentionally covers the track map, the documented Control
// Change assignments, and the System Exclusive envelope (Parameter Change /
// Bulk Dump / Dump Request / Parameter Request / Identity Request-Reply)
// needed to route and remote-control a SEQTRAK from the MK2 bridge. The full
// per-parameter SysEx address table in the data list runs to ~90 pages of
// sound-design parameters; only the top-level block addresses actually
// needed for transport/mixer-level control are included here. Extend
// `ParamAddress` constants as needed using the PDF's "MIDI Data Table"
// section (Address columns are Yamaha's standard 3-byte High/Mid/Low scheme).
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace seqtrak {

// ---------------------------------------------------------------------------
// Track map (SEQTRAK Data List, "Track Type", p.2)
// ---------------------------------------------------------------------------

enum class TrackGroup { kDrum, kSynth, kDx, kSampler };

struct TrackInfo {
  const char* name;
  TrackGroup group;
  int midi_channel;  // 1-based
};

constexpr TrackInfo kTracks[] = {
    {"KICK", TrackGroup::kDrum, 1},     {"SNARE", TrackGroup::kDrum, 2},
    {"CLAP", TrackGroup::kDrum, 3},     {"HAT1", TrackGroup::kDrum, 4},
    {"HAT2", TrackGroup::kDrum, 5},     {"PERC1", TrackGroup::kDrum, 6},
    {"PERC2", TrackGroup::kDrum, 7},    {"SYNTH1", TrackGroup::kSynth, 8},
    {"SYNTH2", TrackGroup::kSynth, 9},  {"DX", TrackGroup::kDx, 10},
    {"SAMPLER", TrackGroup::kSampler, 11},
};
constexpr size_t kTrackCount = sizeof(kTracks) / sizeof(kTracks[0]);
constexpr int kFirstMidiChannel = 1;
constexpr int kLastMidiChannel = 11;

// ---------------------------------------------------------------------------
// Control Change map (MIDI Data Format §3-1-3, transmit + receive tables;
// also see MIDI Implementation Chart, p.162)
// ---------------------------------------------------------------------------
//
// Values marked [tx] are transmitted by SEQTRAK; [rx] are recognized on
// input; most are both. Value ranges/meanings per the data list.

constexpr uint8_t kCcBankSelectMsb = 0;    // [tx/rx] 0-127, see bank table
constexpr uint8_t kCcPortamentoTime = 5;   // [tx/rx] 0-127
constexpr uint8_t kCcDataEntryMsb = 6;     // [rx only] RPN data entry
constexpr uint8_t kCcTrackVolume = 7;      // [tx/rx] 0-127
constexpr uint8_t kCcPan = 10;             // [tx/rx] 0-127 (L63..C..R63)
constexpr uint8_t kCcExpression = 11;      // [rx only] 0-127
constexpr uint8_t kCcBankSelectLsb = 32;   // [tx/rx] 0-127
constexpr uint8_t kCcDataEntryLsb = 38;    // [rx only] RPN data entry
constexpr uint8_t kCcEqHighGain = 20;      // [tx/rx] 40:-12dB .. 64:0 .. 88:+12dB
constexpr uint8_t kCcEqLowGain = 21;       // [tx/rx] 40:-12dB .. 64:0 .. 88:+12dB
constexpr uint8_t kCcMute = 23;            // [rx only] 0-63 off, 64-127 on
constexpr uint8_t kCcSolo = 24;            // [rx only] 0 off, 1..11 = track
constexpr uint8_t kCcDrumPitch = 25;       // [tx/rx] 40:-24 .. 64:0 .. 88:+24 (Drum track)
constexpr uint8_t kCcMonoPolyChord = 26;   // [tx/rx] 0=Mono,1=Poly,2=Chord (Synth/DX)
constexpr uint8_t kCcArpTemplate = 27;     // [tx/rx] 0-15 (0=off) (Synth/DX)
constexpr uint8_t kCcArpGate = 28;         // [tx/rx] 0:0% .. 127:200% (Synth/DX)
constexpr uint8_t kCcArpSpeed = 29;        // [tx/rx] 0:200% .. 3:100% .. 9:25% (Synth/DX)
constexpr uint8_t kCcSustainSwitch = 64;   // [rx only] 0-127
constexpr uint8_t kCcPortamentoSwitch = 65;  // [tx/rx] 0=off,1=on
constexpr uint8_t kCcSostenuto = 66;       // [rx only] 0-63 off, 64-127 on
constexpr uint8_t kCcFilterResonance = 71;  // [tx/rx] 0:-64 .. 64:0 .. 127:+63
constexpr uint8_t kCcEgAttackTime = 73;    // [tx/rx] 0:-64 .. 64:0 .. 127:+63
constexpr uint8_t kCcFilterCutoffFreq = 74;  // [tx/rx] 0:-64 .. 64:0 .. 127:+63
constexpr uint8_t kCcEgDecayReleaseTime = 75;  // [tx/rx] 0:-64 .. 64:0 .. 127:+63
constexpr uint8_t kCcReverbSend = 91;      // [tx/rx] 0-127
constexpr uint8_t kCcDelaySend = 94;       // [tx/rx] 0-127
constexpr uint8_t kCcDataEntryInc = 96;    // [rx only] RPN increment (value=127)
constexpr uint8_t kCcDataEntryDec = 97;    // [rx only] RPN decrement (value=127)
constexpr uint8_t kCcRpnLsb = 100;         // [rx only]
constexpr uint8_t kCcRpnMsb = 101;         // [rx only]
constexpr uint8_t kCcAllSoundOff = 120;    // [rx only, channel mode]
constexpr uint8_t kCcResetAllControllers = 121;  // [rx only, channel mode]
constexpr uint8_t kCcAllNotesOff = 123;    // [rx only, channel mode]
constexpr uint8_t kCcMonoModeOn = 126;     // [rx only, channel mode]
constexpr uint8_t kCcPolyModeOn = 127;     // [rx only, channel mode]

// Model-specific "assigned parameter" CCs (102-119), routed to whichever
// effect/operator slot is currently selected on the SEQTRAK UI. See
// MIDI Data Format table footnote *4 for the full mapping.
constexpr uint8_t kCcMasterEffect1Param1 = 102;
constexpr uint8_t kCcMasterEffect1Param2 = 103;
constexpr uint8_t kCcMasterEffect1Param3 = 104;
constexpr uint8_t kCcMasterEffect2Param1 = 105;
constexpr uint8_t kCcMasterEffect3Param1 = 106;
constexpr uint8_t kCcSingleEffectParam1 = 107;
constexpr uint8_t kCcSingleEffectParam2 = 108;
constexpr uint8_t kCcSingleEffectParam3 = 109;
constexpr uint8_t kCcSendReverbParam1 = 110;
constexpr uint8_t kCcSendReverbParam2 = 111;
constexpr uint8_t kCcSendReverbParam3 = 112;
constexpr uint8_t kCcSendDelayParam1 = 113;
constexpr uint8_t kCcSendDelayParam2 = 114;
constexpr uint8_t kCcSendDelayParam3 = 115;
constexpr uint8_t kCcFmAlgorithm = 116;      // DX track only, 0-11
constexpr uint8_t kCcFmModulationAmount = 117;  // DX track only
constexpr uint8_t kCcFmModulatorFrequency = 118;  // DX track only
constexpr uint8_t kCcFmModulatorFeedback = 119;   // DX track only

// RPN (Registered Parameter Number) targets, MSB/LSB pairs written via CC 101
// (RPN MSB) + CC 100 (RPN LSB), value via CC 6/38 (data entry MSB/LSB).
struct RpnTarget {
  uint8_t msb;
  uint8_t lsb;
};
constexpr RpnTarget kRpnPitchBendSensitivity = {0x00, 0x00};  // 0-24 semitones, data entry MSB only
constexpr RpnTarget kRpnMasterFineTune = {0x00, 0x01};        // +-8192/8192 semitone, MSB+LSB
constexpr RpnTarget kRpnMasterCoarseTune = {0x00, 0x02};      // 28-40-58 (-24..0..+24 semitones), MSB only
constexpr RpnTarget kRpnReset = {0x7F, 0x7F};

// ---------------------------------------------------------------------------
// MIDI channel voice message basics (MIDI Data Format §3-1)
// ---------------------------------------------------------------------------

constexpr int kFirstChannelNumber = 0;   // channel n in "1001nnnn" etc.
constexpr int kLastChannelNumber = 10;   // 11 tracks -> channels 0..10

// Bank Select MSB/LSB -> Program Change ranges (MIDI Data Table, p.117-121).
// Program Change selects the sound/element/project within the selected bank.
struct BankSelect {
  uint8_t msb;
  uint8_t lsb;
  const char* description;
};
constexpr BankSelect kBankProjectUser1 = {0x40, 0x00, "Project User 1 (temporary project at PC=8)"};
constexpr BankSelect kBankSoundPresetBase = {0x3F, 0x00, "Drum/Synth/DX Sound Preset bank 1 (LSB 0x00-0x1F = presets 1-32)"};
constexpr BankSelect kBankSoundUserBase = {0x3F, 0x20, "Drum/Synth/DX Sound User bank 1 (LSB 0x20-0x2F = user 1-16)"};
constexpr BankSelect kBankSamplerElementPresetBase = {0x3E, 0x00, "SAMPLER Element Preset bank (LSB 0x00-0x03 = presets 1-4)"};
constexpr BankSelect kBankSamplerElementUserBase = {0x3E, 0x04, "SAMPLER Element User bank (LSB 0x04-0x0B = user 1-8)"};
constexpr BankSelect kBankDrumKitBase = {
    0x20, 0x00,
    "Drum Kit Sound bank; MSB 0x20-0x26 selects drum part 1-7"};

// ---------------------------------------------------------------------------
// System Exclusive envelope (MIDI Data Format §3-5)
// ---------------------------------------------------------------------------
//
// All SysEx messages share the Yamaha header:
//   F0 43 <device> <group-hi> <group-lo> ...
// with group = 7F 1C (fixed for SEQTRAK) and Model ID = 0C.
//
//   Parameter Change:  F0 43 1n 7F 1C 0C ah am al dd [dd...] F7
//   Bulk Dump:         F0 43 0n 7F 1C bh bl 0C ah am al dd [dd...] cc F7
//   Dump Request:      F0 43 2n 7F 1C 0C ah am al F7
//   Parameter Request: F0 43 3n 7F 1C 0C ah am al F7
//   Identity Request:  F0 7E 0n 06 01 F7                     (n ignored)
//   Identity Reply:    F0 7E 7F 06 02 43 00 41 dd dd mm 00 00 7F F7
//
// n            = device number (0 in the low nibble selects "device 1"; the
//                high nibble of that byte carries the message-type selector
//                shown above: 0n=bulk dump, 1n=param change, 2n=dump
//                request, 3n=param request).
// ah, am, al   = 7-bit parameter address (High, Mid, Low), see ParamAddress.
// bh, bl       = 14-bit byte count of the dump payload (Model ID onward, not
//                including the checksum), split 7+7 bits.
// dd           = data byte(s), width matches the parameter (1, 2, or 4 bytes
//                as documented per-parameter in the data list; multi-byte
//                fields are big-endian nibble/7-bit-group packed, see
//                individual parameter comments in the PDF).
// cc           = bulk dump checksum: seven low bits of
//                -(byte_count_hi + byte_count_lo + address_hi + address_mid
//                  + address_lo + sum(data bytes)) mod 128.
// dd dd (identity reply) = device family number/code; SEQTRAK = 0x64 0x06.
// mm (identity reply)    = firmware version encoded as (version - 1.0) * 10,
//                e.g. v1.0 -> 0, v1.5 -> 5.

constexpr uint8_t kSysExStart = 0xF0;
constexpr uint8_t kSysExEnd = 0xF7;
constexpr uint8_t kYamahaManufacturerId = 0x43;

constexpr uint8_t kSysExGroupHigh = 0x7F;
constexpr uint8_t kSysExGroupLow = 0x1C;
constexpr uint8_t kSysExModelId = 0x0C;

// High nibble of the third SysEx byte selects the message type; low nibble
// is the device number (n).
constexpr uint8_t kSysExTypeBulkDump = 0x00;
constexpr uint8_t kSysExTypeParameterChange = 0x10;
constexpr uint8_t kSysExTypeDumpRequest = 0x20;
constexpr uint8_t kSysExTypeParameterRequest = 0x30;

// Universal Non-Realtime identity messages (not Yamaha-specific header).
constexpr uint8_t kSysExUniversalNonRealtime = 0x7E;
constexpr uint8_t kIdentityRequestSubId1 = 0x06;
constexpr uint8_t kIdentityRequestSubId2Request = 0x01;
constexpr uint8_t kIdentityRequestSubId2Reply = 0x02;
constexpr std::array<uint8_t, 2> kSeqtrakDeviceFamily = {0x64, 0x06};

// ---------------------------------------------------------------------------
// Top-level parameter block addresses (MIDI Data Table, "Parameter Base
// Address"). Each is a 3-byte {High, Mid, Low} address for use with
// Parameter Change / Dump Request / Parameter Request. `p` = part number
// placeholder (0-10 for the 11 tracks; substitute into Mid or Low nibble as
// documented per block), `e` = sampler element number (0-6).
// ---------------------------------------------------------------------------

struct ParamAddress {
  uint8_t high;
  uint8_t mid;
  uint8_t low;
};

constexpr ParamAddress kAddrSystemGeneral = {0x00, 0x00, 0x00};

// System General fields relevant to getting SysEx replies over USB at all
// (MIDI Data Table p.122): each transport (Legacy/USB/Bluetooth) has its own
// independent "Transmit System Exclusive Message" on/off flag, and all three
// default to Off. Receiving a Parameter/Dump Request is gated separately (by
// the transport's "In/Out" flag, which defaults On for USB) -- so SEQTRAK can
// receive a request fine while still being unable to transmit the reply
// until this flag is turned on. Confirmed as the likely cause of a real
// no-reply-received test result on 2026-07-21 (empirically grounded per this
// table, not yet independently confirmed against the actual device
// behavior).
constexpr int kSystemGeneralLegacyMidiTransmitSysExOffset = 0x0C;
constexpr int kSystemGeneralUsbMidiTransmitSysExOffset = 0x10;
constexpr int kSystemGeneralBluetoothMidiTransmitSysExOffset = 0x14;

constexpr ParamAddress kAddrFormatVersion = {0x00, 0x7F, 0x00};
constexpr ParamAddress kAddrBulkHeader = {0x11, 0x00, 0x00};
constexpr ParamAddress kAddrBulkFooter = {0x12, 0x00, 0x00};

constexpr ParamAddress kAddrProjectCommonGeneral = {0x30, 0x40, 0x00};
constexpr ParamAddress kAddrProjectSendReverb = {0x30, 0x41, 0x00};
constexpr ParamAddress kAddrProjectSendDelay = {0x30, 0x42, 0x00};
constexpr ParamAddress kAddrProjectMasterEffect1 = {0x30, 0x43, 0x00};
constexpr ParamAddress kAddrProjectMasterEffect2 = {0x30, 0x44, 0x00};
constexpr ParamAddress kAddrProjectMasterEffect3 = {0x30, 0x45, 0x00};
constexpr ParamAddress kAddrProjectMasterEffect4 = {0x30, 0x46, 0x00};
constexpr ParamAddress kAddrProjectMasterEq = {0x30, 0x47, 0x00};
constexpr ParamAddress kAddrProjectAdInsertionA = {0x30, 0x49, 0x00};
constexpr ParamAddress kAddrProjectAdInsertionB = {0x30, 0x4A, 0x00};
constexpr ParamAddress kAddrProjectAdGeneral = {0x30, 0x4B, 0x00};
constexpr ParamAddress kAddrProjectUsbAudioInput = {0x30, 0x4C, 0x00};
constexpr ParamAddress kAddrProjectScale = {0x30, 0x4D, 0x00};

// Project General offset 0x76 (2 bytes, MSB/LSB 7-bit split): tempo, 5-300
// BPM. Offset 0x66 (1 byte): master Volume 0-127.
constexpr int kProjectGeneralTempoOffset = 0x76;
constexpr int kProjectGeneralVolumeOffset = 0x66;
constexpr int kProjectGeneralPanOffset = 0x65;
constexpr int kProjectGeneralKeyOffset = 0x7F;   // 0x40=key 0 .. +11 semitones
constexpr int kProjectGeneralScaleOffset = 0x7E;  // 0-7 = Scale 1-8

// Project Track blocks: address Mid byte = 0x5p/0x6p/0x7p where p = part
// number (see kAddrProjectTrackGeneral etc., substitute p into the low
// nibble of Mid). Cross-checked against the "Parameter Base Address" master
// table, p.118.
constexpr uint8_t kAddrProjectTrackGeneralMidBase = 0x50;  // 0x50 | p, p=0-10 (all tracks)
// p=7-9 only (SYNTH1/SYNTH2/DX) per p.118 -- General covers all 11 tracks,
// these two Chord Notes blocks do not.
constexpr uint8_t kAddrProjectTrackChordScale14MidBase = 0x60;  // 0x60 | p
constexpr uint8_t kAddrProjectTrackChordScale58MidBase = 0x70;  // 0x70 | p
constexpr int kTrackGeneralVolumeOffset = 0x00;
constexpr int kTrackGeneralPanOffset = 0x01;
constexpr int kTrackGeneralOctaveOffset = 0x0C;  // 0x40 = 0, +/-2
constexpr int kTrackGeneralMuteOffset = 0x29;    // 0x00=off, 0x7D=on

// Project Track General address for track part 0-10 and one of the offsets
// above (confirmed against MIDI Data Table p.134: high=0x30, mid=0x50|p).
constexpr ParamAddress ProjectTrackGeneralAddress(int part, int offset) {
  return {0x30, static_cast<uint8_t>(kAddrProjectTrackGeneralMidBase | part),
          static_cast<uint8_t>(offset)};
}

// Sound Common blocks, high=0x31, mid = base | p. Part ranges per the
// "Parameter Base Address" master table, p.118 -- Name/General/Insertion A/B
// cover all 11 tracks (p=0-10); LFO excludes DX (p=0-8,10); Arpeggiator is
// SYNTH1/SYNTH2/DX only (p=7-9). Confirmed empirically (2026-07-21): a
// Parameter Request for General (0x31 0x1p 0x0D, Trigger/Gate Mode) DID get a
// reply -- unlike Project Track General (0x30 block), which did not (see
// [[project-seqtrak-sysex-write-only]] in memory; still being mapped out
// which blocks actually reply).
constexpr uint8_t kAddrSoundCommonNameMidBase = 0x00;     // p=0-10
constexpr uint8_t kAddrSoundCommonGeneralMidBase = 0x10;  // p=0-10
constexpr uint8_t kAddrSoundInsertionAMidBase = 0x20;     // p=0-10
constexpr uint8_t kAddrSoundInsertionBMidBase = 0x30;     // p=0-10
constexpr uint8_t kAddrSoundLfoMidBase = 0x40;            // p=0-8,10 (no DX)
constexpr uint8_t kAddrSoundArpeggiatorMidBase = 0x50;    // p=7-9 (SYNTH1/SYNTH2/DX only)

// Sound Drum/Synth/SAMPLER (AWM Sound Element), Oscillator/Amplitude/Pitch
// block (MIDI Data Table p.147-148): mid = (element*0x10 | part), where
// element e is 0-7 (Element 1-8) and part p is 0-8 or 0x0A (Part 1-9, 11 --
// part 9/0x09 = DX track is skipped, DX has no AWM elements, see
// kAddrDxOperatorHigh instead). Low byte is the per-field offset below.
//
// This is the block that actually holds per-element sound assignment: a
// track's overall Sound/Kit (selected via Bank Select + Program Change,
// kBankSoundPresetBase et al.) only picks a preset *name*; which sample/wave
// plays for each of a track's 8 elements is a separate, per-element setting
// only reachable here, via SysEx Parameter Change -- Program Change cannot
// set it. This is what a "Drum Kit" track's 8 individual pad/element sounds
// are actually made of (confirmed against SEQTRAK_data_list_En_D0.pdf p.147,
// as raised by the user).
constexpr uint8_t kAddrElementOscAmpPitchHigh = 0x41;
constexpr int kElementAssignOffset = 0x00;      // 0=Off, 1=On (element 0 defaults on, 1-7 default off)
constexpr int kElementWaveSelectOffset = 0x01;  // 0=Preset, 1=User (SAMPLER track fixed to 1/User)
constexpr int kElementGroupNumberOffset = 0x02; // 1-8
constexpr int kElementWaveNumberMsbOffset = 0x03;  // 2 bytes MSB/LSB, 7+7 bit split
constexpr int kElementWaveNumberLsbOffset = 0x04;  // Preset: 1-4096, User: 1-2048

constexpr uint8_t kAddrElementFilterEqLfoHigh = 0x42;

constexpr ParamAddress kAddrDxCommon = {0x48, 0x09, 0x00};
constexpr uint8_t kAddrDxOperatorHigh = 0x49;  // mid = operator*0x10 | 0x09

constexpr uint8_t kAddrSamplerSampleHigh = 0x50;  // mid = element*0x10 | 0x0A

// ---------------------------------------------------------------------------
// Bulk dump byte counts for the whole-project / whole-sound / whole-element
// headers (MIDI Data Table, "Bulk Dump Block"). Useful to size receive
// buffers when requesting a full dump instead of individual parameters.
// ---------------------------------------------------------------------------

constexpr int kBulkDumpSystemGeneralBytes = 0x34;
constexpr int kBulkDumpProjectCommonGeneralBytes = 0x84;
constexpr int kBulkDumpProjectTrackGeneralBytes = 0x2E;
constexpr int kBulkDumpSoundNameBytes = 0x6E;
constexpr int kBulkDumpSoundGeneralBytes = 0x54;
constexpr int kBulkDumpElementOscAmpPitchBytes = 0x71;
constexpr int kBulkDumpElementFilterEqLfoBytes = 0x4B;
constexpr int kBulkDumpDxCommonBytes = 0x1C;
constexpr int kBulkDumpDxOperatorBytes = 0x24;
constexpr int kBulkDumpSamplerSampleBytes = 0x70;

}  // namespace seqtrak
