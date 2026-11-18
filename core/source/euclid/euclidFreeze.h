#pragma once

#include "euclidEngine.h"

#include <stdint.h>

// Freeze: turns the notes the engine played into tracks for a stock pattern.
//
// Channel N's notes go to pattern track N (tracks 1-8), all as MIDI. A
// channel whose notes repeated exactly through the window gets a track that
// loops at that repeat length, so a 13-step channel freezes as a 13-step
// track and keeps drifting; anything else keeps the whole window as a take.
// Track 1 sets the pattern's length, so channel 1 always fills the whole
// pattern. One track plays one note per step, so chords that match a stock
// Chord FX shape stay on one track, and other extra notes (wider chords,
// 1/32 notes) go to voice tracks: tracks 9-16, then the tracks of channels
// that played nothing. Notes that don't fit are dropped, highest first.
//
// This file has no platform dependencies; euclidFreezePlatform.cpp writes
// the result into a pattern.

namespace euclid
{

constexpr int kFreezeTracks = 16;
constexpr int kFreezeMaxSteps = 128;
constexpr int kFreezeFirstVoiceTrack = 8; // tracks 9-16 hold extra voices

struct FrozenStep
{
    static constexpr int8_t kEmpty = -1;
    static constexpr int8_t kNoteOff = -2;

    int8_t note = kEmpty; // 0-127, kEmpty or kNoteOff
    uint8_t midiChannel = 1;
    uint8_t volume = 0;      // 0-100, the pattern's volume column
    int8_t micromove = -1;   // 0-100 or -1, the pattern's micromove column
    int16_t gate = -1;       // Gate-length FX value 0-100, or -1 for none
    uint8_t chord = 0;       // stock Chord FX index, 0 for none
};

struct FrozenTrack
{
    bool used = false;
    uint8_t channel = 0; // engine channel
    uint8_t voice = 0;   // 0 = the channel's main track
    uint8_t length = 0;  // steps this track loops at
    FrozenStep step[kFreezeMaxSteps];
};

struct FrozenPattern
{
    uint8_t length = 0; // steps (the window)
    FrozenTrack track[kFreezeTracks];
    uint8_t tracksUsed = 0;
    uint8_t channels = 0;     // channels that played something
    uint8_t loopingTracks = 0; // tracks shorter than the pattern
    uint16_t droppedNotes = 0;
    bool incomplete = false; // the oldest notes of the window had been overwritten
};

// Builds a pattern from the notes played in [start, end) micro-steps, which
// must be whole 1/16 steps and at most kFreezeMaxSteps long.
void buildFreeze(const Engine& engine, uint32_t start, uint32_t end, FrozenPattern& out);

// Stock Chord FX index for notes sounding together (sorted or not), or 0 if
// no stock shape matches. *root gets the lowest note.
uint8_t matchStockChord(const uint8_t* notes, uint8_t count, uint8_t* root);

} // namespace euclid
