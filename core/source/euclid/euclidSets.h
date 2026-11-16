#pragma once

#include "euclidEngine.h"

#include <stddef.h>
#include <stdint.h>

// Sets: everything the 8 channels are doing, stored as a small binary file.
//
// Layout (version 1, 268 bytes, little-endian where it matters):
//   0  'E' 'U' 'C' 'S'
//   4  version (1)
//   5  channel count (8)
//   6  slot this state was last saved to or loaded from (1-8, 0 = none)
//   7  reserved (0)
//   8  8 channel records of 32 bytes:
//        steps, pulses, rotation, division, direction, noteCount,
//        notes[16], enabled, muted, midiChannel, velocity, gate,
//        probability, 4 reserved bytes
//   264 CRC-32 of bytes 0-263
//
// Records are written field by field rather than as raw structs, so the
// format doesn't change with compiler padding or struct edits.

namespace euclid
{

constexpr uint8_t kSetVersion = 1;
constexpr uint8_t kSetSlots = 8;
constexpr size_t kSetHeaderSize = 8;
constexpr size_t kSetChannelSize = 32;
constexpr size_t kSetFileSize = kSetHeaderSize + kChannels * kSetChannelSize + 4;

uint32_t crc32(const uint8_t* data, size_t length);

// Writes the engine's settings (the pending ones, which is what the screen
// shows) into buf. Returns the number of bytes written, or 0 if buf is too
// small.
size_t serializeSet(const Engine& engine, uint8_t slot, uint8_t* buf, size_t bufLen);

// Checks a set file and, if it's valid, applies it to the engine. While
// playing, rhythm changes wait for the next bar as usual. Returns false and
// leaves the engine alone if the data is the wrong size, version or CRC.
bool deserializeSet(Engine& engine, const uint8_t* buf, size_t len, uint8_t* slotOut = nullptr);

} // namespace euclid
