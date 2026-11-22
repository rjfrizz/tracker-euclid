#pragma once

#include <stdint.h>

// Generative Euclidean MIDI engine for the EUCLID edition.
//
// The engine is driven by the stock sequencer's micro-step clock
// (48 micro-steps per 1/16 step, 192 per beat) and only runs while the
// sequencer is playing, so it follows the Tracker's tempo, external sync,
// MIDI clock output and transport.
//
// Settings live in three layers per channel:
//   staged    - what the screen edits and shows
//   committed - sent by commit() (the Rec key) or an immediate edit (Shift
//               held), waiting for the next launch point
//   playing   - what is heard now
// Edits go to the staged layer. commit() moves staged settings to the
// committed layer, and they start playing at the next launch point (a beat,
// a bar, 2 bars or 4 bars). With setImmediate(true), each edit is committed
// as it's made: rhythm, key and mode changes still wait for the launch point
// so the timing stays tight, while velocity, gate, chance, MIDI channel and
// note edits play straight away. While stopped every edit applies at once.
// Mute is never staged.
//
// Threading: onMicroStep(), start() and stop() run from the audio callback.
// Everything else is called from the main loop, protected by the platform's
// interrupt guard.
//
// This file has no platform dependencies so the timing logic can be tested
// on a host machine (see core/tests/euclid/).

namespace euclid
{

constexpr int kChannels = 8;
constexpr int kMaxSteps = 32;
constexpr int kMaxNotes = 16;      // notes in a SEQ list, shared by its entries
constexpr int kMaxVoices = 6;      // notes a channel can play at once
constexpr int kMaxEntryNotes = 6;  // notes in one SEQ chord entry
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
    scaleUp,    // climbs the scale, wrapping at the top of the range
    scaleUpDown,
    arpUp,      // climbs the chord's tones across the range
    arpUpDown,
    count
};

// Chords, as scale steps stacked on the note played, so they follow the
// channel's scale. Stored in set files, so only add at the end.
enum class ChordType : uint8_t
{
    single = 0,
    triad,   // 1 3 5
    seventh, // 1 3 5 7
    ninth,   // 1 3 5 7 9
    sus2,    // 1 2 5
    sus4,    // 1 4 5
    power,   // 1 5
    quartal, // stacked 4ths
    count
};

const char* chordTypeName(ChordType t);
// Scale steps above the note for each tone, the first always 0.
uint8_t chordSteps(ChordType t, uint8_t* steps);

// Which key a channel follows.
//   Generated modes: follow = the global key, offset = the global scale with
//   the root keyOffset semitones up, own = ownRoot and ownScale.
//   SEQ: own = the list exactly as entered; follow and offset move the list
//   from its home key to that key and snap stray notes into the scale.
enum class KeyMode : uint8_t
{
    follow = 0,
    offset,
    own,
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

// Rhythm settings. A change to steps, pulses, rotation, division or mode
// restarts the channel when it starts playing; note edits don't.
//
// The SEQ list is notes[0 .. noteCount-1] split into entries: bit i of
// tieMask set means note i is played together with note i - 1, so a chord
// entry is a run of tied notes. Entries play in order, one per pulse.
struct Rhythm
{
    uint8_t steps = 16;
    uint8_t pulses = 4;
    uint8_t rotation = 0;
    Division division = Division::d1_16;
    uint8_t noteCount = 1;
    uint8_t notes[kMaxNotes] = {60};
    uint16_t tieMask = 0;
    NoteDirection direction = NoteDirection::forward;
    NoteMode mode = NoteMode::seq;

    // The global key when the note list was last edited: what a following
    // SEQ channel transposes from.
    uint8_t homeRoot = 0;
    uint8_t homeScale = 1; // Minor

    // Steps changed by hand (the Pattern page's pads), bit per step: added
    // pulses and muted ones. Editing steps, pulses or rotation clears them.
    uint32_t forceOn = 0;
    uint32_t forceOff = 0;
};

// Whether step i plays: the Euclidean pattern with the hand changes.
bool stepIsPulse(const Rhythm& r, uint8_t i);

// Expression settings: how the channel's notes are played.
// Modulators. Each channel has four (A-D). A modulator's shape gives a value
// that's scaled by its amount and added to its destination; any number can
// share a destination, and they add up.
//
// Smooth shapes run on the bar grid from Play, one cycle per `rate`, offset
// by `phase`, and swing both ways around the setting (-1..+1):
//   sine, triangle, sawUp, sawDown, square
//   smoothRandom - glides to a new random level each cycle
//   drift        - wanders: each cycle's level is a small random step from
//                  the last, glided to, so it never quite repeats
//   randomSteps  - a new random level each cycle, held
// Euclidean shapes use the modulator's own rhythm (steps, pulses, rotation,
// division, auto-rotation every few bars) and only push one way (0..1):
//   euclidAccent   - full on pulses, nothing between
//   euclidEnvelope - jumps on a pulse, then falls away over `length` steps
//   euclidRandom   - a new random level on each pulse, held
// A modulator can also drive another one on the same channel (`target`):
// its depth (modAmount: 0 to twice the amount set) or its speed (modRate:
// half to twice as fast).
//
// Stored in set files, so only add at the end of these enums.
enum class ModDest : uint8_t
{
    off = 0,
    velocity,
    gate,
    chance,
    pitch, // amount -100..+100 = -24..+24 semitones
    cc,    // sent on the channel's MIDI channel; see Engine::updateMods
    modAmount,
    modRate,
    count
};

enum class ModShape : uint8_t
{
    euclidAccent = 0,
    euclidEnvelope,
    euclidRandom,
    sine,
    triangle,
    square,
    randomSteps,
    sawUp,
    sawDown,
    smoothRandom,
    drift,
    count
};

// Cycle lengths for the smooth shapes.
enum class ModRate : uint8_t
{
    r1_16 = 0,
    r1_8,
    r1_4,
    r1_2,
    bar1,
    bar2,
    bar4,
    bar8,
    bar16,
    bar32,
    bar64,
    count
};

struct Mod
{
    ModDest dest = ModDest::off;
    ModShape shape = ModShape::sine;
    int8_t amount = 50; // -100..+100
    ModRate rate = ModRate::bar1;
    uint8_t phase = 0;  // percent of a cycle, 0-99
    uint8_t target = 0; // modulator 0-3 for modAmount / modRate
    // Euclidean shapes:
    uint8_t steps = 16;
    uint8_t pulses = 4;
    uint8_t rotation = 0;
    Division division = Division::d1_16;
    uint8_t autoRotate = 0; // bars between rotations: 0 (off), 1, 2, 4 or 8
    uint8_t length = 4;     // euclidEnvelope: decay, in its steps
    uint8_t cc = 74;        // MIDI CC number for ModDest::cc
};

constexpr int kModsPerChannel = 4;

const char* modDestName(ModDest d);
const char* modShapeName(ModShape s);
const char* modRateName(ModRate r);
uint32_t modRateToMicroSteps(ModRate r);
bool isEuclidShape(ModShape s);
// The rate nearest to a period, in micro-steps (for older set files).
ModRate modRateNearest(uint32_t microSteps);

// Shift: a hand-drawn lane of transpositions over whatever the channel
// plays. Per step, the lane follows the channel's step grid (so a lane of a
// different length drifts against the rhythm); per pulse, each note takes
// the next value. Scale steps move notes within the channel's key. Stored in
// set files, so only add at the end of these enums.
enum class ShiftAdvance : uint8_t
{
    perStep = 0,
    perPulse,
    perCycle, // once each time the rhythm comes round to its first step
    count
};

enum class ShiftUnits : uint8_t
{
    semitones = 0,
    scaleSteps,
    count
};

constexpr int kShiftSteps = 32;
constexpr int kMaxShift = 24;

struct Shift
{
    bool on = false;
    uint8_t length = 16; // 1-32
    ShiftAdvance advance = ShiftAdvance::perStep;
    ShiftUnits units = ShiftUnits::semitones;
    int8_t step[kShiftSteps] = {};
};

// More Shift lanes, beside the pitch one: each step sets the channel's
// mode, division, note order (SEQ) or chord for the notes it applies to, or
// is kLaneNone (the channel's own setting). Each lane has its own length and
// advance; a division lane only changes per cycle, so a cycle keeps one step
// length. Stored in set files, so only add at the end.
enum class LaneKind : uint8_t
{
    mode = 0,
    division,
    order,
    chord,
    count
};
constexpr int kLaneKinds = static_cast<int>(LaneKind::count);
constexpr int8_t kLaneNone = -1;

struct Lane
{
    bool on = false;
    uint8_t length = 4; // 1-32
    ShiftAdvance advance = ShiftAdvance::perCycle;
    int8_t step[kShiftSteps];
    Lane()
    {
        for (int8_t& v : step)
            v = kLaneNone;
    }
};

// How many values a lane's steps can take (besides none).
uint8_t laneValueCount(LaneKind k);
// A step's value, short enough for a grid cell ("-" for none).
const char* laneValueName(LaneKind k, int8_t value);

// Stored in set files, so only add at the end.
enum class ChannelOutput : uint8_t
{
    midi = 0,
    sampler,
    count
};
constexpr uint8_t kSampleInstruments = 48;

struct Expression
{
    bool enabled = false;
    bool muted = false;
    uint8_t midiChannel = 1; // 1-16
    uint8_t velocity = 100;  // 1-127
    uint8_t gate = 50;       // percent of the channel's step length, 1-100
    uint8_t probability = 100; // percent, 0-100

    // Key. Generated modes and SEQ each have their own key mode, so
    // switching Mode doesn't change how a channel follows the key.
    KeyMode keyMode = KeyMode::follow; // generated modes
    KeyMode seqKeyMode = KeyMode::own; // SEQ
    uint8_t keyOffset = 7; // semitones, 1-11 (KeyMode::offset, both)
    uint8_t ownRoot = 0;   // 0-11 (KeyMode::own, generated)
    uint8_t ownScale = 2;  // Major (KeyMode::own, generated)
    uint8_t octave = 3;    // lowest octave, Tracker numbering (C3 = note 36)
    uint8_t range = 2;     // octaves, 1-4
    int8_t transpose = 0;  // semitones, -24 to +24, every mode, after the key

    // Chords, every mode. Arps use the chord's tones (Single counts as a
    // triad) and ignore chordChance.
    ChordType chord = ChordType::single;
    uint8_t chordChance = 100; // percent of pulses that play the chord

    // Where the notes go: MIDI (the channel's MIDI channel), or the
    // project's sample instrument on the audio track of the same number. A
    // track plays one note at a time, so a chord plays its first note.
    ChannelOutput output = ChannelOutput::midi;
    uint8_t instrument = 0; // 0-based sample instrument

    Mod mod[kModsPerChannel];
    Shift shift;
    Lane lanes[kLaneKinds]; // by LaneKind
};

// SEQ list entries.
uint8_t entryCount(const Rhythm& r);
uint8_t entryStart(const Rhythm& r, uint8_t entry);
uint8_t entryLength(const Rhythm& r, uint8_t entry);

constexpr int kMaxTranspose = 24;

// When committed changes start playing. Stored in set files, so the
// values must not change.
enum class Launch : uint8_t
{
    bar = 0,
    beat,
    twoBars,
    fourBars,
    count
};

// Channel names, shown on the screen keys. 0 is no name ("Ch 1"). Stored in
// set files by index, so only add names at the end.
uint8_t channelNameCount();
const char* channelName(uint8_t index); // nullptr for 0

uint16_t launchToMicroSteps(Launch l);
const char* launchName(Launch l);

// A note the engine played, for freezing. Time is in micro-steps since Play.
struct NoteEvent
{
    uint32_t time;
    uint16_t gate; // micro-steps
    uint8_t channel; // engine channel 0-7
    uint8_t note;
    uint8_t velocity;
    uint8_t midiChannel;
};

// Freeze length, saved with sets.
enum class FreezeLength : uint8_t
{
    fourBars = 0,
    oneBar,
    twoBars,
    eightBars,
    count
};
uint8_t freezeLengthBars(FreezeLength f);

struct Output
{
    void (*noteOn)(uint8_t note, uint8_t velocity, uint8_t channel) = nullptr;
    void (*noteOff)(uint8_t note, uint8_t channel) = nullptr;
    // Sampler output: a note on channel ch's audio track, and its end.
    void (*sampleOn)(uint8_t ch, uint8_t instrument, uint8_t note, uint8_t velocity) = nullptr;
    void (*sampleOff)(uint8_t ch) = nullptr;
    void (*controlChange)(uint8_t cc, uint8_t value, uint8_t channel) = nullptr;
    // -8192..8191, 0 = none.
    void (*pitchBend)(int16_t value, uint8_t channel) = nullptr;
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

    // Freeze (the Perform page): everything holds where it is, the notes
    // sounding stay on and nothing moves on (the sequencer stops calling
    // onMicroStep, so the clock pauses too). Each channel's next notes are
    // worked out on scratch (a copy of the engine run on). setMorph slides
    // each channel from the notes it held towards those: a single MIDI note
    // glides with pitch bend (in steps of bendRange semitones, re-striking
    // past it), chords and sampler channels move a voice at a time, the
    // nearest first. unfreeze lands on whichever is nearer and carries on
    // from there: with the next notes, as if their pulse had just played.
    void freeze(Engine& scratch);
    void setMorph(int amount); // 0 (as held) .. 1000 (the next notes)
    int getMorph() const { return morphAmount; }
    void unfreeze();
    bool isFrozen() const { return frozen; }
    void setBendRange(uint8_t semitones) { bendRange = semitones < 1 ? 1 : (semitones > 48 ? 48 : semitones); }
    uint8_t getBendRange() const { return bendRange; }
    // The notes a channel held and its next ones (0 if none), for the screen.
    uint8_t getHeldNotes(int ch, uint8_t* notes) const;
    uint8_t getNextNotesFor(int ch, uint8_t* notes) const;

    // Transport, called from the sequencer.
    void start();
    void stop();
    void onMicroStep();

    //--------------------------------------------------------------------------
    // Main-loop API

    // Staged settings: what the screen shows and edits.
    Rhythm getRhythm(int ch) const;
    Expression getExpression(int ch) const;
    Key getGlobalKey() const { return globalStaged; }
    // The key a channel plays in, from its staged settings. For a SEQ
    // channel set to Own it's the list's home key.
    Key getChannelKey(int ch) const;

    // Committed settings (playing, or waiting for the launch point).
    Rhythm getLiveRhythm(int ch) const;
    Expression getLiveExpression(int ch) const;
    Key getLiveGlobalKey() const { return globalDirty ? globalPending : globalActive; }

    // What is playing right now.
    Rhythm getActiveRhythm(int ch) const;
    Key getActiveGlobalKey() const { return globalActive; }

    // Edits. They change the staged settings; see the file comment for when
    // they're heard.
    void setRhythm(int ch, const Rhythm& r);
    void setExpression(int ch, const Expression& e);
    void setSteps(int ch, uint8_t steps);
    void setPulses(int ch, uint8_t pulses);
    void setRotation(int ch, uint8_t rotation);
    // A step by hand: a pulse is muted, a rest becomes a pulse, and a
    // changed step goes back to the pattern.
    void toggleStep(int ch, uint8_t step);
    void setDivision(int ch, Division d);
    void setMode(int ch, NoteMode m);
    void setNotes(int ch, const uint8_t* notes, uint8_t count, uint16_t tieMask = 0);
    void setDirection(int ch, NoteDirection d);
    void setEnabled(int ch, bool enabled);
    void setMidiChannel(int ch, uint8_t midiChannel);
    void setVelocity(int ch, uint8_t velocity);
    void setGate(int ch, uint8_t gate);
    void setProbability(int ch, uint8_t probability);
    void setGlobalKey(const Key& k);

    // Mute bypasses staging: it's a performance control.
    void setMuted(int ch, bool muted);
    // A mute that changes at the next launch point (or now, while stopped),
    // for loading sets and scenes in time with everything else.
    void setMutedAtLaunch(int ch, bool muted);

    // A channel's name (channelName() index). Just a label: never staged.
    uint8_t getName(int ch) const;
    void setName(int ch, uint8_t index); // also clears a text name
    // A name of its own (a synth patch's, say), which shows instead of the
    // list's. Empty for none.
    static constexpr int kTextNameLength = 10;
    const char* getTextName(int ch) const;
    void setTextName(int ch, const char* text);
    // What the channel is called: its text name, the list name, or nullptr.
    const char* getNameShown(int ch) const;

    // While true, every edit is committed as it's made (Shift held).
    void setImmediate(bool immediate) { immediateEdits = immediate; }

    // Staged edits that haven't been committed.
    bool hasStagedChange(int ch) const;
    bool hasStagedGlobalKey() const;
    bool hasAnyStagedChange() const;

    // Sends staged settings to play from the next launch point.
    void commit(int ch);
    void commitAll(); // all channels and the global key

    // Committed changes waiting for the launch point.
    bool hasPendingChange(int ch) const;
    bool hasPendingGlobalKey() const { return globalDirty; }

    Launch getLaunch() const { return launch; }
    void setLaunch(Launch l);

    FreezeLength getFreezeLength() const { return freezeLength; }
    void setFreezeLength(FreezeLength f);

    // Whether the engine's notes are heard (and captured). The engine keeps
    // running either way, so turning it back on lands in time.
    bool isOutputEnabled() const { return outputEnabled; }
    void setOutputEnabled(bool enabled);
    // Layer: play the live engine over Pattern and Song playback too.
    // Saved with sets.
    bool getLayer() const { return layer; }
    void setLayer(bool on);

    //--------------------------------------------------------------------------
    // Capture: every note played since Play is kept in a ring buffer (the
    // newest `capacity` notes) so it can be frozen into a pattern.
    void setCaptureBuffer(NoteEvent* buffer, uint32_t capacity);
    uint32_t getPlayTime() const { return playTime; } // micro-steps since Play
    // Notes are numbered from 0 since Play; the oldest still kept is
    // captureFirst(), and captureEnd() is one past the newest.
    uint32_t captureFirst() const;
    uint32_t captureEnd() const { return captureCount; }
    const NoteEvent& captureAt(uint32_t index) const { return captureBuffer[index % captureCapacity]; }
    // How long a channel's output takes to repeat with its current
    // settings, in micro-steps, assuming nothing random is involved (the
    // freeze checks that against what was actually played). 0 if unknown.
    uint32_t repeatLength(int ch) const;

    // A modulator's value right now (-1000..1000; Euclidean shapes 0..1000)
    // and where its rhythm is, for the screen.
    int16_t getModValue(int ch, int mod) const;
    uint8_t getModStep(int ch, int mod) const;
    uint8_t getModRotation(int ch, int mod) const; // including auto-rotation
    // The Shift step the channel last played (for the screen), or -1.
    int8_t getShiftStep(int ch) const;
    int8_t getLaneStep(int ch, LaneKind k) const; // the step used last, -1 for none
    // The notes the channel played last (they stay until the next pulse that
    // plays), and how many; 0 before any.
    uint8_t getLastNotes(int ch, uint8_t* notes) const;
    // The Shift lane's pitch step playing now (semitones or scale steps, as
    // set), 0 if none; and a note moved by it, as the channel plays it.
    int8_t getShiftAmount(int ch) const;
    uint8_t shiftedNote(int ch, uint8_t note) const;
    uint32_t getNoteCount(int ch) const; // changes each time it plays
    // The notes a generated mode can play, low to high (the scale across the
    // range, or for arps the chord's tones), at most max; 0 for SEQ.
    uint8_t getNotePool(int ch, uint8_t* notes, uint8_t max) const;

    // Goes up on every settings change, for autosave and for screens that
    // need to notice changes made elsewhere (such as loading a set).
    uint32_t getChangeCount() const { return changeCount; }

    // Playback position, for the UI.
    bool isRunning() const { return running; }
    uint8_t getCurrentStep(int ch) const;
    // Index of the SEQ entry the next pulse will play.
    uint8_t getNextNoteIndex(int ch) const;
    uint32_t getBarMicroStep() const { return launchCounter % kMicroStepsPerBar; }

  private:
    struct Channel
    {
        Rhythm staged;
        Expression stagedExpr;

        Rhythm pending;
        Expression pendingExpr;
        bool pendingDirty = false;
        bool pendingMute = false; // pendingExpr.muted applies at the launch point

        Rhythm active;
        Expression expr;

        uint16_t microStepInStep = 0; // 0 .. stepLength-1
        uint8_t step = 0;             // 0 .. active.steps-1
        uint8_t noteIndex = 0;
        int8_t pingPongDir = 1;
        uint32_t shiftCounter = 0; // Shift steps advanced since the channel started
        int8_t shiftPlayed = -1;   // Shift step used last, for the screen
        uint8_t shiftIndex = 0;    // the Shift step in use (per cycle: all cycle long)
        uint32_t laneCounter[kLaneKinds] = {};
        uint8_t laneIndex[kLaneKinds] = {};
        int8_t lanePlayed[kLaneKinds] = { -1, -1, -1, -1 };
        Division stepDivision = Division::d1_16; // this cycle's (a division lane)
        int16_t degree = 0; // position in the scale, generated modes
        int8_t degreeDir = 1;

        uint8_t openCount = 0; // notes sounding
        uint8_t openNotes[kMaxVoices] = {};
        uint8_t openMidiChannel = 1;
        bool openSampler = false; // the open note is on the audio track
        uint8_t lastNotes[kMaxVoices] = {};
        uint8_t lastCount = 0;
        uint32_t notesPlayed = 0;
        uint16_t gateRemaining = 0;

        struct ModState
        {
            uint32_t phase = 0;   // smooth shapes: position in the cycle, 2^32 = one cycle
            uint32_t cycle = 0;   // smooth shapes: cycles completed
            uint32_t lastStep = 0xFFFFFFFFu; // Euclidean shapes: last step seen
            int16_t from = 0;     // random shapes: level at the start of the cycle
            int16_t to = 0;       // random shapes: level at the end of it
            int16_t value = 0;    // -1000..1000, updated every micro-step
        } modState[kModsPerChannel];
        int16_t lastCc[kModsPerChannel] = { -1, -1, -1, -1 }; // last value sent, by first modulator of a CC
        int16_t modEffectiveAmount[kModsPerChannel] = {};     // amount after other modulators' depth
    };

    void lock() const;
    void unlock() const;
    void clampRhythm(Rhythm& r) const;
    Expression clampExpression(const Expression& e) const;
    void resetChannelPosition(Channel& c);
    void closeNote(Channel& c);
    void triggerStep(Channel& c);
    uint8_t nextNotes(Channel& c, uint8_t* notes);
    uint8_t nextSeqNotes(Channel& c, uint8_t* notes);
    uint8_t nextGeneratedNotes(Channel& c, uint8_t* notes);
    bool rollChord(const Channel& c);
    int shiftNote(const Channel& c, int note, int8_t amount) const;
    uint8_t modRotationAt(const Mod& m, uint32_t t) const;
    void morphChannel(int ch, int amount);
    void setSounding(Channel& c, const uint8_t* notes, uint8_t count);
    void updateMods(Channel& c);
    void resetMods(Channel& c);
    int randomLevel(); // -1000..1000
    Key keyFor(const Expression& x, const Key& global) const;
    Key seqKeyFor(const Expression& x, const Rhythm& r, const Key& global) const;
    uint8_t seqNote(const Channel& c, uint8_t listNote) const;

    // After an edit to channel ch's staged settings; call with the lock held.
    void edited(int ch);
    void commitLocked(int ch, bool immediate);
    void commitGlobalLocked();
    void applyPending(Channel& c);

    Output out;
    Channel channels[kChannels];
    bool running = false;
    bool immediateEdits = false;
    Launch launch = Launch::bar;
    uint32_t launchCounter = 0; // micro-steps since start, wraps every 4 bars
    uint32_t changeCount = 0;   // main loop only
    FreezeLength freezeLength = FreezeLength::fourBars;
    bool outputEnabled = true;
    bool layer = false;

    uint32_t playTime = 0;
    NoteEvent* captureBuffer = nullptr;
    uint32_t captureCapacity = 0;
    volatile uint32_t captureCount = 0;

    uint8_t names[kChannels] = {};
    char textNames[kChannels][kTextNameLength + 1] = {};

    Key globalStaged;
    Key globalPending;
    Key globalActive;
    bool globalDirty = false;

    // Freeze and morph.
    struct Morph
    {
        uint8_t from[kMaxVoices] = {};
        uint8_t fromCount = 0;
        uint8_t to[kMaxVoices] = {};
        uint8_t toCount = 0;
        bool hasNext = false;
        bool glide = false;      // one MIDI note to one: pitch bend
        int16_t bend = 0;        // last bend sent
        // The channel's note choosing just after the next notes' pulse.
        uint8_t noteIndex = 0;
        int8_t pingPongDir = 1;
        int16_t degree = 0;
        int8_t degreeDir = 1;
    };
    Morph morph[kChannels];
    bool frozen = false;
    int morphAmount = 0;
    uint8_t bendRange = 2;
};

void loadDemoDefaults(Engine& e);

} // namespace euclid

extern euclid::Engine euclidEngine;
