#pragma once

#include <stddef.h>
#include <stdint.h>

// Sets up the Euclidean engine's MIDI output and loads its startup defaults.
// Call once at boot, before the sequencer starts.
void euclidPlatformInit();

// Gives the engine its capture buffer (euclidFreezePlatform.cpp).
void euclidFreezeInit();

// What a Play press plays, decided by the screen it's pressed on: on the
// EUCLID screen the live engine only (the stock pattern keeps time but stays
// silent); anywhere else the stock pattern or song, plus the live engine if
// Layer is on. Called by the sequencer when playback starts.
void euclidRoutePlay();
// True while the EUCLID screen is showing (fEuclidParams.cpp).
bool euclidScreenActive();

// Main-loop housekeeping: autosaves the working state to the SD card a short
// while after the last change.
void euclidPlatformUpdate();

// Sets on the SD card, in Sets/. Slots are numbered 1-8. The working state
// is also kept in Sets/current.eus and restored at boot.
namespace euclidSets
{
// Loads the working state saved before the last power-off. Call once the SD
// card is ready (the EUCLID screen does this the first time it opens).
// Autosave stays off until this has run, so it can't overwrite the saved
// state with the startup demo.
void bootLoad();

bool save(uint8_t slot);
bool load(uint8_t slot);
bool exists(uint8_t slot);

// The slot the working state came from or was last saved to, 0 if none.
uint8_t currentSlot();
// True if something changed since that save or load.
bool editedSinceSlot();
} // namespace euclidSets

namespace euclidFreeze
{
// Freezes the notes played in [start, end) micro-steps (whole bars) into a
// new pattern in the first free slot. The message says what happened.
bool freeze(uint32_t start, uint32_t end, char* message, size_t messageLength);
} // namespace euclidFreeze
