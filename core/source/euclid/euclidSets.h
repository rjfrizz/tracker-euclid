#pragma once

#include "euclidEngine.h"

#include <stddef.h>
#include <stdint.h>

// Sets: everything the 8 channels are doing, plus the global key, stored as
// a small binary file.
//
// Layout (version 7, 1140 bytes):
//   0  'E' 'U' 'C' 'S'
//   4  version (7)
//   5  channel count (8)
//   6  slot this state was last saved to or loaded from (1-8, 0 = none)
//   7  global root (0-11)
//   8  global scale (stock scales.h index)
//   9  launch point (euclid::Launch; 0 = bar, so older version 2 files load
//      with the default)
//   10 freeze length (euclid::FreezeLength; 0 = 4 bars, the default)
//   11 layer (1 = the live engine also plays over Pattern and Song; 0 = off)
//   12 reserved (4 bytes of 0)
//   16 8 channel records of 140 bytes:
//        0 steps, pulses, rotation, division, direction, noteCount
//        6 notes[16]
//        22 enabled, muted, midiChannel, velocity, gate, probability
//        28 mode, keyMode, keyOffset, ownRoot, ownScale, octave, range
//        35 seqKeyMode, transpose + 64, homeRoot, homeScale
//        39 name (euclid::channelName index; 0 = none)
//        40 tieMask (2 bytes, little-endian), chord, chordChance
//        44 output (euclid::ChannelOutput; 0 = MIDI), sample instrument, then
//           2 reserved (0s; files before version 9 have 0s here too)
//        48 4 modulators of 14 bytes: dest, shape, amount + 128, rate,
//           phase, target, steps, pulses, rotation, division, autoRotate,
//           length, cc, then delay (bits 0-3) and fade (bits 4-7), 0 in
//           files from before them
//        104 Shift: on, length, advance, units, then 32 steps (value + 64)
//        140 text name (10 characters, 0-padded, then a 0; shown instead of
//            the list name when it isn't empty)
//        151 reserved
//        152 4 more Shift lanes (mode, division, order, chord) of 35 bytes:
//            on, length, advance, then 32 steps (value + 1; 0 = none)
//        292 steps changed by hand: added, then muted (bit per step, 4
//            bytes each, little-endian)
//        300 the Sample and Pulses lanes, laid out as the other lanes
//            (version 11)
//   2976 CRC-32 (little-endian) of bytes 0-2975
//
// Older files still load, and the fields they don't have get defaults:
//   version 10 (2420 bytes: 300-byte records, CRC at 2416): no Sample lane.
//   version 9 (2356 bytes: 292-byte records, CRC at 2352): no hand changes.
//   version 8 (1236 bytes: 152-byte records, CRC at 1232): no extra lanes.
//   version 7 (1140 bytes: 140-byte records, CRC at 1136): no text names.
//   version 6 (852 bytes: 104-byte records, CRC at 848): no Shift.
//   version 5 (596 bytes: 72-byte records, CRC at 592): two modulators of
//     12 bytes (dest, shape, amount + 128, steps, pulses, rotation, division,
//     autoRotate, length, cc); their LFO period (length steps of division)
//     becomes the nearest rate, and modulators C and D are off.
//   version 4 (404 bytes: 48-byte records, CRC at 400): no modulators.
//   version 3 (340 bytes: 40-byte records, CRC at 336): no chords; byte 39
//     is the name (0 in files written before names existed).
//   version 2 (340 bytes): no bytes 35-38; SEQ lists are Own, untransposed,
//     with the file's global key as their home key.
//   version 1 (268 bytes: an 8-byte header, 32-byte records without the key
//     fields, CRC at 264): also no global key, launch or bytes 28-38.
//
// Records are written field by field rather than as raw structs, so the
// format doesn't change with compiler padding or struct edits.

namespace euclid
{

constexpr uint8_t kSetVersion = 11;
constexpr uint8_t kSetSlots = 8;
constexpr size_t kSetHeaderSize = 16;
constexpr size_t kSetChannelSize = 370;
constexpr size_t kSetSampleLaneOffset = 300; // then the Pulses lane
constexpr size_t kSetForceOffset = 292;
constexpr size_t kSetTextNameOffset = 140;
constexpr size_t kSetLanesOffset = 152;
constexpr size_t kSetLaneSize = 35;
constexpr size_t kSetShiftOffset = 104;
constexpr size_t kSetModSize = 14;
constexpr size_t kSetFileSize = kSetHeaderSize + kChannels * kSetChannelSize + 4;

// Version 10, read only.
constexpr size_t kSetV10ChannelSize = 300;
constexpr size_t kSetV10FileSize = kSetHeaderSize + kChannels * kSetV10ChannelSize + 4;

// Version 9, read only.
constexpr size_t kSetV9ChannelSize = 292;
constexpr size_t kSetV9FileSize = kSetHeaderSize + kChannels * kSetV9ChannelSize + 4;

// Version 8, read only.
constexpr size_t kSetV8ChannelSize = 152;
constexpr size_t kSetV8FileSize = kSetHeaderSize + kChannels * kSetV8ChannelSize + 4;

// Version 7, read only.
constexpr size_t kSetV7ChannelSize = 140;
constexpr size_t kSetV7FileSize = kSetHeaderSize + kChannels * kSetV7ChannelSize + 4;

// Version 6, read only.
constexpr size_t kSetV6ChannelSize = 104;
constexpr size_t kSetV6FileSize = kSetHeaderSize + kChannels * kSetV6ChannelSize + 4;

// Version 5, read only.
constexpr size_t kSetV5ChannelSize = 72;
constexpr size_t kSetV5ModSize = 12;
constexpr size_t kSetV5FileSize = kSetHeaderSize + kChannels * kSetV5ChannelSize + 4;

// Version 4, read only.
constexpr size_t kSetV4ChannelSize = 48;
constexpr size_t kSetV4FileSize = kSetHeaderSize + kChannels * kSetV4ChannelSize + 4;

// Versions 2 and 3, read only.
constexpr size_t kSetV3ChannelSize = 40;
constexpr size_t kSetV3FileSize = kSetHeaderSize + kChannels * kSetV3ChannelSize + 4;

// Version 1, read only.
constexpr size_t kSetV1HeaderSize = 8;
constexpr size_t kSetV1ChannelSize = 32;
constexpr size_t kSetV1FileSize = kSetV1HeaderSize + kChannels * kSetV1ChannelSize + 4;

uint32_t crc32(const uint8_t* data, size_t length);

// User progressions in a small file (Sets/progress.prg, and with projects):
// "EUPR", version 1, the count, then each one's name (16 bytes), slots used
// and 16 slots of root, scale and beats; a CRC-32 at the end.
constexpr size_t kProgFileSize = 8 + kUserProgs * (kProgNameLength + 1 + kProgSlots * 3) + 4;
size_t serializeProgressions(const Progression* user, uint8_t* buf, size_t length);
bool deserializeProgressions(Progression* user, const uint8_t* buf, size_t length);

// Writes the engine's staged settings (what the screen shows, including edits
// not yet committed) into buf. Returns the number of bytes written, or 0 if buf is too
// small.
size_t serializeSet(const Engine& engine, uint8_t slot, uint8_t* buf, size_t bufLen);

// Checks a set file (version 1 or 2) and, if it's valid, applies it to the
// engine and commits it, so while playing it starts at the next launch
// point.
// Returns false and leaves the engine alone if the data is the wrong size,
// version or CRC.
bool deserializeSet(Engine& engine, const uint8_t* buf, size_t len, uint8_t* slotOut = nullptr);

} // namespace euclid
