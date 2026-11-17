#pragma once

#include "euclidEngine.h"

#include <stddef.h>
#include <stdint.h>

// Sets: everything the 8 channels are doing, plus the global key, stored as
// a small binary file.
//
// Layout (version 3, 340 bytes):
//   0  'E' 'U' 'C' 'S'
//   4  version (3)
//   5  channel count (8)
//   6  slot this state was last saved to or loaded from (1-8, 0 = none)
//   7  global root (0-11)
//   8  global scale (stock scales.h index)
//   9  launch point (euclid::Launch; 0 = bar, so older version 2 files load
//      with the default)
//   10 reserved (6 bytes of 0)
//   16 8 channel records of 40 bytes:
//        0 steps, pulses, rotation, division, direction, noteCount
//        6 notes[16]
//        22 enabled, muted, midiChannel, velocity, gate, probability
//        28 mode, keyMode, keyOffset, ownRoot, ownScale, octave, range
//        35 seqKeyMode, transpose + 64, homeRoot, homeScale
//        39 reserved (0)
//   336 CRC-32 (little-endian) of bytes 0-335
//
// Older files still load, and the fields they don't have get defaults:
//   version 2 (340 bytes): no bytes 35-38; SEQ lists are Own, untransposed,
//     with the file's global key as their home key.
//   version 1 (268 bytes: an 8-byte header, 32-byte records without the key
//     fields, CRC at 264): also no global key, launch or bytes 28-38.
//
// Records are written field by field rather than as raw structs, so the
// format doesn't change with compiler padding or struct edits.

namespace euclid
{

constexpr uint8_t kSetVersion = 3;
constexpr uint8_t kSetSlots = 8;
constexpr size_t kSetHeaderSize = 16;
constexpr size_t kSetChannelSize = 40;
constexpr size_t kSetFileSize = kSetHeaderSize + kChannels * kSetChannelSize + 4;

// Version 1, read only.
constexpr size_t kSetV1HeaderSize = 8;
constexpr size_t kSetV1ChannelSize = 32;
constexpr size_t kSetV1FileSize = kSetV1HeaderSize + kChannels * kSetV1ChannelSize + 4;

uint32_t crc32(const uint8_t* data, size_t length);

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
