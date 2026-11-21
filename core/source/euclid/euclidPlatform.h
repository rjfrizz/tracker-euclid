#pragma once

#include <stddef.h>
#include <stdint.h>

// Sets up the Euclidean engine's MIDI output and loads its startup defaults.
// Call once at boot, before the sequencer starts.
void euclidPlatformInit();

// What a Play press plays: the live engine, with the stock sequencer keeping
// time silently. Called by the sequencer when playback starts.
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

// Scenes A-D: snapshots of all 8 channels and the global key, kept in
// memory and saved with the working state and with sets (a .scn file next
// to the set). Recalling one lands at the next launch point. What was
// playing before the first recall is kept: returnHome() goes back to it,
// and edits made in a scene don't change it (or the scene, unless saved).
namespace euclidScenes
{
constexpr uint8_t kScenes = 4;
bool save(uint8_t scene);   // 0-3
bool recall(uint8_t scene); // false if the scene is empty
bool has(uint8_t scene);
bool returnHome(); // false if no scene was playing
int8_t active();   // the scene playing, or -1 for home
} // namespace euclidScenes

// The chain (Song key; euclidChain.h): saved in Sets/chain.chn. While it's
// on, stopping readies its first step and Play runs the steps in order.
namespace euclid
{
struct Chain;
}
namespace euclidChain
{
euclid::Chain& get();
void changed(); // after editing get(): saves it
// The step playing (-1 if none), the bar within it and its length.
int8_t playingStep(uint32_t* barInStep, uint32_t* stepBars);
} // namespace euclidChain

// The synth editor (euclidSynthPlatform.cpp): synth definitions from
// Synths/*.syn, which one each channel edits, and the values being edited.
// Edits go out straight away (they aren't staged): the synth plays them.
namespace euclid
{
namespace synth
{
struct Definition;
}
} // namespace euclid
namespace euclidSynth
{
constexpr int kMaxFiles = 24;
constexpr int kNameLength = 24;

void init();
void pump(uint32_t nowUs); // audio callback: sends queued SysEx
void update();             // main loop: feeds a whole-patch send to the queue
void scan();               // re-reads the list of definition files
uint8_t files();
const char* file(uint8_t index);
bool assign(uint8_t ch, int fileIndex); // -1 for none; false (see error()) if it can't load
int assignedFile(uint8_t ch);           // -1 for none
const euclid::synth::Definition* definition(uint8_t ch);
uint8_t device(uint8_t ch);
void setDevice(uint8_t ch, uint8_t device);
uint8_t ports(uint8_t ch); // 0 wherever notes go, 1 DIN, 2 USB, 3 both
void setPorts(uint8_t ch, uint8_t ports);
// Which of a multitimbral synth's parts the channel edits, 1 .. With the
// setting 0 (Auto, for synths that say which MIDI channel part 1 is on) it
// follows the channel's MIDI channel, and is 0 when that isn't a part.
uint8_t part(uint8_t ch);
uint8_t partSetting(uint8_t ch);
void setPart(uint8_t ch, uint8_t part);
int16_t value(uint8_t ch, uint16_t param);
void setValue(uint8_t ch, uint16_t param, int value); // and sends it
void sendAll(uint8_t ch);                            // every param, to match the synth to the screen
bool sending();
// Asks the synth for its patch; the reply sets the values. False (see
// error()) if the definition has no request.
bool requestPatch(uint8_t ch);
bool receiving();
uint32_t receiveEvents();     // counts replies and timeouts
const char* receiveMessage(); // what the last one was
void receiveDin(const uint8_t* data, uint16_t length, bool complete); // from MIDI input
void receiveUsb(const uint8_t* data, uint16_t length, bool complete);
const char* error();
void saveAssignments();
void loadAssignments();

// The patch library (Synths/<synth>/<name>.syx). Saving needs a patch got
// from the synth (or loaded) first: it's those dumps with the page's values
// written in. Loading sends the dumps to the channel's device and part.
uint8_t scanPatches(uint8_t ch); // the channel's synth's patches; their count
uint8_t patches();
const char* patch(uint8_t index); // its name, without .syx
bool savePatch(uint8_t ch, char* savedAs, size_t length);
bool loadPatch(uint8_t ch, uint8_t index);
bool deletePatch(uint8_t ch, uint8_t index);
} // namespace euclidSynth
