#pragma once

#include <stdint.h>

// Generative Euclidean MIDI engine for the EUCLID edition.
//
// The engine is driven by the stock sequencer's micro-step clock
// (48 micro-steps per 1/16 step, 192 per beat) and only runs while the
// sequencer is playing, so it follows the Tracker's tempo, external sync,
// MIDI clock output and transport.
//
// Threading: onMicroStep(), start() and stop() run from the audio callback.
// The setters below are called from the main loop and write to a channel's
// "pending" settings, protected by the platform's interrupt guard. Rhythm
// settings (steps, pulses, rotation, division) take effect on the next bar;
// everything else, including note edits, takes effect immediately.
//
// This file has no platform dependencies so the timing logic can be tested
// on a host machine (see core/tests/euclid/).

namespace euclid
{

constexpr int kChannels = 8;
constexpr int kMaxSteps = 32;
constexpr int kMaxNotes = 16;
constexpr int kMicroStepsPerStep = 48; // one 1/16 note
constexpr int kStepsPerBar = 16;       // 4/4
constexpr int kMicroStepsPerBar = kMicroStepsPerStep * kStepsPerBar;

enum class Division : uint8_t
{
    d1_32 = 0,
    d1_16T,
    d1_16,
    d1_8T,
    d1_8,
    d1_4,
    count
};

enum class NoteDirection : uint8_t
{
    forward = 0,
    backward,
    pingPong,
    random,
    count
};

// Where a channel's notes come from: its own list (SEQ) or generated from
// its key.
enum class NoteMode : uint8_t
{
    seq = 0,
    random,   // any note of the scale within the range
    walk,     // moves up or down by up to two scale steps
    arpUp,    // climbs the scale, wrapping at the top of the range
    arpUpDown,
    count
};

// Which key a generated channel uses.
enum class KeyMode : uint8_t
{
    follow = 0, // the global key
    offset,     // the global scale, root moved up by keyOffset semitones
    own,        // its own root and scale
    count
};

// Root 0-11 (C = 0) and a scale index from the stock scales.h table.
struct Key
{
    uint8_t root = 0;
    uint8_t scale = 1; // Minor
};

uint16_t divisionToMicroSteps(Division d);
const char* divisionName(Division d);
const char* noteModeName(NoteMode m);
uint8_t scaleCount();
const char* scaleName(uint8_t scale);
const char* rootName(uint8_t root);

// Euclidean pattern: true at index i if step i is a pulse, after rotation.
// Rotation shifts the pattern later in time (a pulse on step 0 moves to
// step `rotation`).
bool isPulse(uint8_t steps, uint8_t pulses, uint8_t rotation, uint8_t index);

// Rhythm settings: steps, pulses, rotation, division and note mode are
// queued to the next bar. The notes and their order live here too but apply
// immediately.
struct Rhythm
{
    uint8_t steps = 16;
    uint8_t pulses = 4;
    uint8_t rotation = 0;
    Division division = Division::d1_16;
    uint8_t noteCount = 1;
    uint8_t notes[kMaxNotes] = {60};
    NoteDirection direction = NoteDirection::forward;
    NoteMode mode = NoteMode::seq;
};

// Expression settings: applied immediately.
struct Expression
{
    bool enabled = false;
    bool muted = false;
    uint8_t midiChannel = 1; // 1-16
    uint8_t velocity = 100;  // 1-127
    uint8_t gate = 50;       // percent of the channel's step length, 1-100
    uint8_t probability = 100; // percent, 0-100

    // Used by the generated note modes.
    KeyMode keyMode = KeyMode::follow;
    uint8_t keyOffset = 7; // semitones, 1-11 (KeyMode::offset)
    uint8_t ownRoot = 0;   // 0-11 (KeyMode::own)
    uint8_t ownScale = 2;  // Major (KeyMode::own)
    uint8_t octave = 3;    // lowest octave, Tracker numbering (C3 = note 36)
    uint8_t range = 2;     // octaves, 1-4
};

struct Output
{
    void (*noteOn)(uint8_t note, uint8_t velocity, uint8_t channel) = nullptr;
    void (*noteOff)(uint8_t note, uint8_t channel) = nullptr;
    // Returns a value in [0, 100).
    uint8_t (*randomPercent)() = nullptr;
    // Returns a value in [0, n).
    uint8_t (*randomBelow)(uint8_t n) = nullptr;
    void (*lock)() = nullptr;
    void (*unlock)() = nullptr;
};

class Engine
{
  public:
    void init(const Output& out);

    // Transport, called from the sequencer.
    void start();
    void stop();
    void onMicroStep();

    // Main-loop API.
    Rhythm getRhythm(int ch) const;        // pending (what the UI shows)
    Rhythm getActiveRhythm(int ch) const;  // what is playing now
    Expression getExpression(int ch) const;
    void setRhythm(int ch, const Rhythm& r);
    void setExpression(int ch, const Expression& e);

    void setSteps(int ch, uint8_t steps);
    void setPulses(int ch, uint8_t pulses);
    void setRotation(int ch, uint8_t rotation);
    void setDivision(int ch, Division d);
    // Applied immediately, also while playing.
    void setNotes(int ch, const uint8_t* notes, uint8_t count);
    void setDirection(int ch, NoteDirection d);
    void setMode(int ch, NoteMode m); // queued to the next bar

    // Global key, queued to the next bar.
    Key getGlobalKey() const { return globalPending; }
    Key getActiveGlobalKey() const { return globalActive; }
    void setGlobalKey(const Key& k);
    bool hasPendingGlobalKey() const { return globalDirty; }
    // The key a channel generates from, given its settings and the pending
    // global key.
    Key getChannelKey(int ch) const;

    void setEnabled(int ch, bool enabled);
    void setMuted(int ch, bool muted);
    void setMidiChannel(int ch, uint8_t midiChannel);
    void setVelocity(int ch, uint8_t velocity);
    void setGate(int ch, uint8_t gate);
    void setProbability(int ch, uint8_t probability);

    bool hasPendingChange(int ch) const;

    // Goes up on every settings change, for autosave and for screens that
    // need to notice changes made elsewhere (such as loading a set).
    uint32_t getChangeCount() const { return changeCount; }

    // Playback position, for the UI.
    bool isRunning() const { return running; }
    uint8_t getCurrentStep(int ch) const;
    // Index of the note the next pulse will play.
    uint8_t getNextNoteIndex(int ch) const;
    uint32_t getBarMicroStep() const { return barMicroStep; }

  private:
    struct Channel
    {
        Rhythm active;
        Rhythm pending;
        Expression expr;
        bool pendingDirty = false;

        uint16_t microStepInStep = 0; // 0 .. stepLength-1
        uint8_t step = 0;             // 0 .. active.steps-1
        uint8_t noteIndex = 0;
        int8_t pingPongDir = 1;
        int16_t degree = 0;    // position in the scale, generated modes
        int8_t degreeDir = 1;

        bool noteOpen = false;
        uint8_t openNote = 0;
        uint8_t openMidiChannel = 1;
        uint16_t gateRemaining = 0;
    };

    void lock() const;
    void unlock() const;
    void clampRhythm(Rhythm& r) const;
    void resetChannelPosition(Channel& c);
    void closeNote(Channel& c);
    void triggerStep(Channel& c);
    uint8_t nextNote(Channel& c);
    uint8_t nextGeneratedNote(Channel& c);
    Key keyFor(const Channel& c, const Key& global) const;
    void markDirty(int ch);
    void applyNotesNow(Channel& c);

    Output out;
    Channel channels[kChannels];
    bool running = false;
    uint32_t barMicroStep = 0; // 0 .. kMicroStepsPerBar-1
    uint32_t changeCount = 0; // main loop only
    Key globalActive;
    Key globalPending;
    bool globalDirty = false;
};

void loadDemoDefaults(Engine& e);

} // namespace euclid

extern euclid::Engine euclidEngine;
