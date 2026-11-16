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
// settings (steps, pulses, rotation, division, note sequence) take effect on
// the next bar; everything else takes effect immediately.
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

uint16_t divisionToMicroSteps(Division d);
const char* divisionName(Division d);

// Euclidean pattern: true at index i if step i is a pulse, after rotation.
// Rotation shifts the pattern later in time (a pulse on step 0 moves to
// step `rotation`).
bool isPulse(uint8_t steps, uint8_t pulses, uint8_t rotation, uint8_t index);

// Rhythm settings: queued, applied on the next bar.
struct Rhythm
{
    uint8_t steps = 16;
    uint8_t pulses = 4;
    uint8_t rotation = 0;
    Division division = Division::d1_16;
    uint8_t noteCount = 1;
    uint8_t notes[kMaxNotes] = {60};
    NoteDirection direction = NoteDirection::forward;
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
    void setNotes(int ch, const uint8_t* notes, uint8_t count);
    void setDirection(int ch, NoteDirection d);

    void setEnabled(int ch, bool enabled);
    void setMuted(int ch, bool muted);
    void setMidiChannel(int ch, uint8_t midiChannel);
    void setVelocity(int ch, uint8_t velocity);
    void setGate(int ch, uint8_t gate);
    void setProbability(int ch, uint8_t probability);

    bool hasPendingChange(int ch) const;

    // Playback position, for the UI.
    bool isRunning() const { return running; }
    uint8_t getCurrentStep(int ch) const;
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
    void markDirty(int ch);

    Output out;
    Channel channels[kChannels];
    bool running = false;
    uint32_t barMicroStep = 0; // 0 .. kMicroStepsPerBar-1
};

void loadDemoDefaults(Engine& e);

} // namespace euclid

extern euclid::Engine euclidEngine;
