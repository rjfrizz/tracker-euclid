#include "euclidEngine.h"

#include "scales.h"

#include <math.h>

euclid::Engine euclidEngine;

namespace euclid
{

namespace
{
template <typename T> T clampValue(T v, T lo, T hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

bool validChannel(int ch)
{
    return ch >= 0 && ch < kChannels;
}
} // namespace

uint16_t divisionToMicroSteps(Division d)
{
    switch (d)
    {
        case Division::d1_32: return 24;
        case Division::d1_16T: return 32;
        case Division::d1_16: return 48;
        case Division::d1_8T: return 64;
        case Division::d1_8: return 96;
        case Division::d1_4: return 192;
        default: return 48;
    }
}

const char* divisionName(Division d)
{
    switch (d)
    {
        case Division::d1_32: return "1/32";
        case Division::d1_16T: return "1/16T";
        case Division::d1_16: return "1/16";
        case Division::d1_8T: return "1/8T";
        case Division::d1_8: return "1/8";
        case Division::d1_4: return "1/4";
        default: return "?";
    }
}

uint16_t launchToMicroSteps(Launch l)
{
    switch (l)
    {
        case Launch::beat: return kMicroStepsPerBar / 4;
        case Launch::twoBars: return kMicroStepsPerBar * 2;
        case Launch::fourBars: return kMicroStepsPerBar * 4;
        default: return kMicroStepsPerBar;
    }
}

namespace
{
const char* const kChannelNames[] = {
    nullptr, "Kick",  "Snare", "Clap",  "Hat",   "Open",  "Perc",  "Tom",   "Rim",
    "Shaker", "Cymbal", "Bass", "Sub",  "Lead",  "Pad",   "Chord", "Arp",   "Keys",
    "Pluck", "Bell",  "Stab",  "Drone", "Vox",   "Brass", "String", "Organ", "Synth",
    "FX",    "Noise", "Riser", "Melody", "Counter", "Fill", "Loop", "Wind",
};
} // namespace

const char* chordTypeName(ChordType t)
{
    switch (t)
    {
        case ChordType::single: return "Single";
        case ChordType::triad: return "Triad";
        case ChordType::seventh: return "7th";
        case ChordType::ninth: return "9th";
        case ChordType::sus2: return "Sus2";
        case ChordType::sus4: return "Sus4";
        case ChordType::power: return "Power";
        case ChordType::quartal: return "Quartal";
        default: return "?";
    }
}

uint8_t chordSteps(ChordType t, uint8_t* steps)
{
    static const uint8_t kTriad[] = { 0, 2, 4 };
    static const uint8_t kSeventh[] = { 0, 2, 4, 6 };
    static const uint8_t kNinth[] = { 0, 2, 4, 6, 8 };
    static const uint8_t kSus2[] = { 0, 1, 4 };
    static const uint8_t kSus4[] = { 0, 3, 4 };
    static const uint8_t kPower[] = { 0, 4 };
    static const uint8_t kQuartal[] = { 0, 3, 6 };

    const uint8_t* src = nullptr;
    uint8_t n = 1;
    switch (t)
    {
        case ChordType::triad: src = kTriad; n = sizeof(kTriad); break;
        case ChordType::seventh: src = kSeventh; n = sizeof(kSeventh); break;
        case ChordType::ninth: src = kNinth; n = sizeof(kNinth); break;
        case ChordType::sus2: src = kSus2; n = sizeof(kSus2); break;
        case ChordType::sus4: src = kSus4; n = sizeof(kSus4); break;
        case ChordType::power: src = kPower; n = sizeof(kPower); break;
        case ChordType::quartal: src = kQuartal; n = sizeof(kQuartal); break;
        default: break;
    }
    for (uint8_t i = 0; i < n; i++)
        steps[i] = src != nullptr ? src[i] : 0;
    return n;
}

uint8_t entryCount(const Rhythm& r)
{
    uint8_t count = 0;
    for (uint8_t i = 0; i < r.noteCount; i++)
        if (i == 0 || !(r.tieMask & (1u << i)))
            count++;
    return count;
}

uint8_t entryStart(const Rhythm& r, uint8_t entry)
{
    uint8_t e = 0;
    for (uint8_t i = 0; i < r.noteCount; i++)
    {
        if (i > 0 && !(r.tieMask & (1u << i)))
            e++;
        if (e == entry)
            return i;
    }
    return 0;
}

uint8_t entryLength(const Rhythm& r, uint8_t entry)
{
    uint8_t start = entryStart(r, entry);
    uint8_t n = 1;
    while (start + n < r.noteCount && (r.tieMask & (1u << (start + n))))
        n++;
    return n;
}

uint8_t channelNameCount()
{
    return sizeof(kChannelNames) / sizeof(kChannelNames[0]);
}

const char* channelName(uint8_t index)
{
    return index < channelNameCount() ? kChannelNames[index] : nullptr;
}

const char* modDestName(ModDest d)
{
    switch (d)
    {
        case ModDest::off: return "Off";
        case ModDest::velocity: return "Velocity";
        case ModDest::gate: return "Gate";
        case ModDest::chance: return "Chance";
        case ModDest::pitch: return "Pitch";
        case ModDest::cc: return "MIDI CC";
        case ModDest::modAmount: return "Mod amount";
        case ModDest::modRate: return "Mod rate";
        default: return "?";
    }
}

const char* modShapeName(ModShape s)
{
    switch (s)
    {
        case ModShape::sine: return "Sine";
        case ModShape::triangle: return "Triangle";
        case ModShape::sawUp: return "Saw up";
        case ModShape::sawDown: return "Saw down";
        case ModShape::square: return "Square";
        case ModShape::smoothRandom: return "Smooth random";
        case ModShape::drift: return "Drift";
        case ModShape::randomSteps: return "Random steps";
        case ModShape::euclidAccent: return "Euclid accent";
        case ModShape::euclidEnvelope: return "Euclid envelope";
        case ModShape::euclidRandom: return "Euclid random";
        default: return "?";
    }
}

const char* modRateName(ModRate r)
{
    switch (r)
    {
        case ModRate::r1_16: return "1/16";
        case ModRate::r1_8: return "1/8";
        case ModRate::r1_4: return "1/4";
        case ModRate::r1_2: return "1/2";
        case ModRate::bar1: return "1 bar";
        case ModRate::bar2: return "2 bars";
        case ModRate::bar4: return "4 bars";
        case ModRate::bar8: return "8 bars";
        case ModRate::bar16: return "16 bars";
        case ModRate::bar32: return "32 bars";
        case ModRate::bar64: return "64 bars";
        default: return "?";
    }
}

uint32_t modRateToMicroSteps(ModRate r)
{
    const uint32_t bar = kMicroStepsPerBar;
    switch (r)
    {
        case ModRate::r1_16: return bar / 16;
        case ModRate::r1_8: return bar / 8;
        case ModRate::r1_4: return bar / 4;
        case ModRate::r1_2: return bar / 2;
        case ModRate::bar2: return bar * 2;
        case ModRate::bar4: return bar * 4;
        case ModRate::bar8: return bar * 8;
        case ModRate::bar16: return bar * 16;
        case ModRate::bar32: return bar * 32;
        case ModRate::bar64: return bar * 64;
        default: return bar;
    }
}

ModRate modRateNearest(uint32_t microSteps)
{
    ModRate best = ModRate::bar1;
    uint32_t bestError = 0xFFFFFFFFu;
    for (uint8_t i = 0; i < static_cast<uint8_t>(ModRate::count); i++)
    {
        uint32_t p = modRateToMicroSteps(static_cast<ModRate>(i));
        uint32_t error = p > microSteps ? p - microSteps : microSteps - p;
        if (error < bestError)
        {
            bestError = error;
            best = static_cast<ModRate>(i);
        }
    }
    return best;
}

bool isEuclidShape(ModShape s)
{
    return s == ModShape::euclidAccent || s == ModShape::euclidEnvelope || s == ModShape::euclidRandom;
}

uint8_t freezeLengthBars(FreezeLength f)
{
    switch (f)
    {
        case FreezeLength::oneBar: return 1;
        case FreezeLength::twoBars: return 2;
        case FreezeLength::eightBars: return 8;
        default: return 4;
    }
}

const char* launchName(Launch l)
{
    switch (l)
    {
        case Launch::beat: return "Beat";
        case Launch::twoBars: return "2 bars";
        case Launch::fourBars: return "4 bars";
        default: return "Bar";
    }
}

const char* noteModeName(NoteMode m)
{
    switch (m)
    {
        case NoteMode::seq: return "SEQ";
        case NoteMode::random: return "Random";
        case NoteMode::walk: return "Walk";
        case NoteMode::scaleUp: return "Scale up";
        case NoteMode::scaleUpDown: return "Scale up-down";
        case NoteMode::arpUp: return "Arp up";
        case NoteMode::arpUpDown: return "Arp up-down";
        default: return "?";
    }
}

uint8_t laneValueCount(LaneKind k)
{
    switch (k)
    {
        case LaneKind::mode: return static_cast<uint8_t>(NoteMode::count);
        case LaneKind::division: return static_cast<uint8_t>(Division::count);
        case LaneKind::order: return static_cast<uint8_t>(NoteDirection::count);
        case LaneKind::chord: return static_cast<uint8_t>(ChordType::count);
        default: return 0;
    }
}

const char* laneValueName(LaneKind k, int8_t v)
{
    if (v < 0 || v >= laneValueCount(k))
        return "-";
    static const char* const modes[] = { "SEQ", "Rand", "Walk", "ScUp", "ScUD", "ArUp", "ArUD" };
    static const char* const orders[] = { "Fwd", "Back", "Pong", "Rand" };
    static const char* const chords[] = { "One", "Triad", "7th", "9th", "Sus2", "Sus4", "Power", "4ths" };
    switch (k)
    {
        case LaneKind::mode: return modes[v];
        case LaneKind::division: return divisionName(static_cast<Division>(v));
        case LaneKind::order: return orders[v];
        case LaneKind::chord: return chords[v];
        default: return "-";
    }
}

uint8_t scaleCount()
{
    return MAX_SCALE + 1;
}

const char* scaleName(uint8_t scale)
{
    return scale <= MAX_SCALE ? scaleNames[scale] : "?";
}

const char* rootName(uint8_t root)
{
    static const char* const names[12] = { "C",  "C#", "D",  "D#", "E",  "F",
                                            "F#", "G",  "G#", "A",  "A#", "B" };
    return names[root % 12];
}

bool stepIsPulse(const Rhythm& r, uint8_t i)
{
    if (i < 32)
    {
        if ((r.forceOn >> i) & 1u)
            return true;
        if ((r.forceOff >> i) & 1u)
            return false;
    }
    return isPulse(r.steps, r.pulses, r.rotation, i);
}

bool isPulse(uint8_t steps, uint8_t pulses, uint8_t rotation, uint8_t index)
{
    if (steps == 0 || pulses == 0)
        return false;
    if (pulses >= steps)
        return true;

    // Bresenham-style distribution, which gives the same set of rhythms as
    // Bjorklund's algorithm up to rotation. The offset makes step 0 a pulse
    // before rotation is applied.
    int i = (index + steps - (rotation % steps)) % steps;
    return ((i * pulses) % steps) < pulses;
}

void Engine::init(const Output& o)
{
    out = o;
    running = false;
    immediateEdits = false;
    launch = Launch::bar;
    launchCounter = 0;
    for (Channel& c : channels)
    {
        c = Channel();
    }
    for (uint8_t& n : names)
        n = 0;
    globalStaged = Key();
    globalPending = Key();
    globalActive = Key();
    globalDirty = false;
}

void Engine::lock() const
{
    if (out.lock)
        out.lock();
}

void Engine::unlock() const
{
    if (out.unlock)
        out.unlock();
}

void Engine::clampRhythm(Rhythm& r) const
{
    r.steps = clampValue<uint8_t>(r.steps, 1, kMaxSteps);
    r.pulses = clampValue<uint8_t>(r.pulses, 0, r.steps);
    r.rotation = r.rotation % r.steps;
    if (r.division >= Division::count)
        r.division = Division::d1_16;
    r.noteCount = clampValue<uint8_t>(r.noteCount, 1, kMaxNotes);
    for (uint8_t& n : r.notes)
        n = clampValue<uint8_t>(n, 0, 127);
    if (r.direction >= NoteDirection::count)
        r.direction = NoteDirection::forward;
    if (r.mode >= NoteMode::count)
        r.mode = NoteMode::seq;
    r.tieMask &= static_cast<uint16_t>(((1u << r.noteCount) - 1) & ~1u);
    r.homeRoot = static_cast<uint8_t>(r.homeRoot % 12);
    if (r.homeScale > MAX_SCALE)
        r.homeScale = 0;
}

Expression Engine::clampExpression(const Expression& e) const
{
    Expression x = e;
    x.midiChannel = clampValue<uint8_t>(x.midiChannel, 1, 16);
    x.velocity = clampValue<uint8_t>(x.velocity, 1, 127);
    x.gate = clampValue<uint8_t>(x.gate, 1, 100);
    x.probability = clampValue<uint8_t>(x.probability, 0, 100);
    if (x.keyMode >= KeyMode::count)
        x.keyMode = KeyMode::follow;
    x.keyOffset = clampValue<uint8_t>(x.keyOffset, 1, 11);
    x.ownRoot = static_cast<uint8_t>(x.ownRoot % 12);
    x.ownScale = clampValue<uint8_t>(x.ownScale, 0, MAX_SCALE);
    x.octave = clampValue<uint8_t>(x.octave, 0, 8);
    x.range = clampValue<uint8_t>(x.range, 1, 4);
    if (x.seqKeyMode >= KeyMode::count)
        x.seqKeyMode = KeyMode::own;
    x.transpose = clampValue<int8_t>(x.transpose, -kMaxTranspose, kMaxTranspose);
    if (x.chord >= ChordType::count)
        x.chord = ChordType::single;
    x.chordChance = clampValue<uint8_t>(x.chordChance, 0, 100);
    if (x.output >= ChannelOutput::count)
        x.output = ChannelOutput::midi;
    if (x.instrument >= kSampleInstruments)
        x.instrument = 0;
    x.shift.length = clampValue<uint8_t>(x.shift.length, 1, kShiftSteps);
    if (x.shift.advance >= ShiftAdvance::count)
        x.shift.advance = ShiftAdvance::perStep;
    if (x.shift.units >= ShiftUnits::count)
        x.shift.units = ShiftUnits::semitones;
    for (int8_t& v : x.shift.step)
        v = clampValue<int8_t>(v, -kMaxShift, kMaxShift);
    for (int k = 0; k < kLaneKinds; k++)
    {
        Lane& l = x.lanes[k];
        l.length = clampValue<uint8_t>(l.length, 1, kShiftSteps);
        if (l.advance >= ShiftAdvance::count || k == static_cast<int>(LaneKind::division))
            l.advance = ShiftAdvance::perCycle;
        for (int8_t& v : l.step)
            if (v < kLaneNone || v >= laneValueCount(static_cast<LaneKind>(k)))
                v = kLaneNone;
    }
    for (Mod& m : x.mod)
    {
        if (m.dest >= ModDest::count)
            m.dest = ModDest::off;
        if (m.shape >= ModShape::count)
            m.shape = ModShape::sine;
        m.amount = clampValue<int8_t>(m.amount, -100, 100);
        if (m.rate >= ModRate::count)
            m.rate = ModRate::bar1;
        m.phase = clampValue<uint8_t>(m.phase, 0, 99);
        m.target = static_cast<uint8_t>(m.target % kModsPerChannel);
        m.steps = clampValue<uint8_t>(m.steps, 1, kMaxSteps);
        m.pulses = clampValue<uint8_t>(m.pulses, 0, m.steps);
        m.rotation = m.rotation % m.steps;
        if (m.division >= Division::count)
            m.division = Division::d1_16;
        if (m.autoRotate != 0 && m.autoRotate != 1 && m.autoRotate != 2 && m.autoRotate != 4 &&
            m.autoRotate != 8)
            m.autoRotate = 0;
        m.length = clampValue<uint8_t>(m.length, 1, kMaxSteps);
        m.cc = clampValue<uint8_t>(m.cc, 0, 127);
    }
    return x;
}

namespace
{
// Changes that restart a channel when they start playing.
bool rhythmRestarts(const Rhythm& a, const Rhythm& b)
{
    return a.steps != b.steps || a.pulses != b.pulses || a.rotation != b.rotation ||
           a.division != b.division || a.mode != b.mode;
}

bool sameNotes(const Rhythm& a, const Rhythm& b)
{
    if (a.noteCount != b.noteCount || a.direction != b.direction || a.homeRoot != b.homeRoot ||
        a.homeScale != b.homeScale || a.tieMask != b.tieMask)
        return false;
    for (uint8_t i = 0; i < a.noteCount; i++)
        if (a.notes[i] != b.notes[i])
            return false;
    return true;
}

bool sameRhythm(const Rhythm& a, const Rhythm& b)
{
    return !rhythmRestarts(a, b) && sameNotes(a, b) && a.forceOn == b.forceOn && a.forceOff == b.forceOff;
}

// Mute is left out: it never goes through staging.
bool sameMod(const Mod& a, const Mod& b)
{
    return a.dest == b.dest && a.shape == b.shape && a.amount == b.amount && a.rate == b.rate &&
           a.phase == b.phase && a.target == b.target && a.steps == b.steps && a.pulses == b.pulses &&
           a.rotation == b.rotation && a.division == b.division && a.autoRotate == b.autoRotate &&
           a.length == b.length && a.cc == b.cc;
}

bool sameShift(const Shift& a, const Shift& b)
{
    if (a.on != b.on || a.length != b.length || a.advance != b.advance || a.units != b.units)
        return false;
    for (int i = 0; i < kShiftSteps; i++)
        if (a.step[i] != b.step[i])
            return false;
    return true;
}

bool sameLane(const Lane& a, const Lane& b)
{
    if (a.on != b.on || a.length != b.length || a.advance != b.advance)
        return false;
    for (int i = 0; i < kShiftSteps; i++)
        if (a.step[i] != b.step[i])
            return false;
    return true;
}

bool sameExpression(const Expression& a, const Expression& b)
{
    if (!sameShift(a.shift, b.shift))
        return false;
    for (int k = 0; k < kLaneKinds; k++)
        if (!sameLane(a.lanes[k], b.lanes[k]))
            return false;
    for (int k = 0; k < kModsPerChannel; k++)
        if (!sameMod(a.mod[k], b.mod[k]))
            return false;
    return a.enabled == b.enabled && a.midiChannel == b.midiChannel && a.velocity == b.velocity &&
           a.gate == b.gate && a.probability == b.probability && a.keyMode == b.keyMode &&
           a.keyOffset == b.keyOffset && a.ownRoot == b.ownRoot && a.ownScale == b.ownScale &&
           a.octave == b.octave && a.range == b.range && a.seqKeyMode == b.seqKeyMode &&
           a.transpose == b.transpose && a.chord == b.chord && a.chordChance == b.chordChance &&
           a.output == b.output && a.instrument == b.instrument;
}

// Key settings change which notes are generated, so like rhythm they wait
// for the launch point even when committed immediately.
bool sameKeySettings(const Expression& a, const Expression& b)
{
    return a.keyMode == b.keyMode && a.keyOffset == b.keyOffset && a.ownRoot == b.ownRoot &&
           a.ownScale == b.ownScale && a.octave == b.octave && a.range == b.range &&
           a.seqKeyMode == b.seqKeyMode && a.transpose == b.transpose;
}

void copyKeySettings(Expression& to, const Expression& from)
{
    to.keyMode = from.keyMode;
    to.keyOffset = from.keyOffset;
    to.ownRoot = from.ownRoot;
    to.ownScale = from.ownScale;
    to.octave = from.octave;
    to.range = from.range;
    to.seqKeyMode = from.seqKeyMode;
    to.transpose = from.transpose;
}

bool sameKey(const Key& a, const Key& b)
{
    return a.root == b.root && a.scale == b.scale;
}
} // namespace

void Engine::resetChannelPosition(Channel& c)
{
    c.microStepInStep = 0;
    c.step = 0;
    c.noteIndex = 0;
    c.pingPongDir = 1;
    c.degree = 0;
    c.degreeDir = 1;
    c.shiftCounter = 0;
    c.shiftPlayed = -1;
    c.shiftIndex = 0;
    for (int k = 0; k < kLaneKinds; k++)
    {
        c.laneCounter[k] = 0;
        c.laneIndex[k] = 0;
        c.lanePlayed[k] = -1;
    }
    c.stepDivision = c.active.division;
}

void Engine::closeNote(Channel& c)
{
    if (c.openCount == 0)
        return;
    if (c.openSampler)
    {
        if (out.sampleOff)
            out.sampleOff(static_cast<uint8_t>(&c - channels));
    }
    else if (out.noteOff)
        for (uint8_t i = 0; i < c.openCount; i++)
            out.noteOff(c.openNotes[i], c.openMidiChannel);
    c.openCount = 0;
    c.gateRemaining = 0;
}

// Committed settings start playing.
void Engine::applyPending(Channel& c)
{
    bool restart = rhythmRestarts(c.active, c.pending);
    c.active = c.pending;
    bool muted = c.pendingMute ? c.pendingExpr.muted : c.expr.muted;
    c.expr = c.pendingExpr;
    c.expr.muted = muted;
    c.pendingDirty = false;
    c.pendingMute = false;

    if (restart)
    {
        closeNote(c);
        resetChannelPosition(c);
    }
    else if (c.noteIndex >= entryCount(c.active))
    {
        c.noteIndex = 0;
    }
}

void Engine::start()
{
    if (globalDirty)
    {
        globalActive = globalPending;
        globalDirty = false;
    }
    for (Channel& c : channels)
    {
        closeNote(c);
        if (c.pendingDirty)
            applyPending(c);
        resetChannelPosition(c);
        resetMods(c);
    }
    launchCounter = 0;
    playTime = 0;
    captureCount = 0; // a new take
    running = true;
}

void Engine::stop()
{
    running = false;
    for (Channel& c : channels)
    {
        closeNote(c);
    }
}

Key Engine::keyFor(const Expression& x, const Key& global) const
{
    Key k = global;
    switch (x.keyMode)
    {
        case KeyMode::offset: k.root = static_cast<uint8_t>((global.root + x.keyOffset) % 12); break;
        case KeyMode::own:
            k.root = x.ownRoot;
            k.scale = x.ownScale;
            break;
        default: break;
    }
    return k;
}

Key Engine::getChannelKey(int ch) const
{
    if (!validChannel(ch))
        return globalStaged;
    const Channel& c = channels[ch];
    if (c.staged.mode == NoteMode::seq)
        return seqKeyFor(c.stagedExpr, c.staged, globalStaged);
    return keyFor(c.stagedExpr, globalStaged);
}

namespace
{
// Semitone offsets of a scale's notes above its root, from the stock 12-bit
// masks (bit n set = n semitones above the root is in the scale).
uint8_t scaleIntervals(const Key& key, uint8_t* intervals)
{
    uint8_t len = 0;
    const uint16_t mask = scaleDef[key.scale <= MAX_SCALE ? key.scale : 0] | 1u;
    for (uint8_t n = 0; n < 12; n++)
        if (mask & (1u << n))
            intervals[len++] = n;
    return len;
}
} // namespace

uint8_t Engine::nextGeneratedNotes(Channel& c, uint8_t* notes)
{
    const Key key = keyFor(c.expr, globalActive);
    uint8_t intervals[12];
    const uint8_t len = scaleIntervals(key, intervals);
    const int16_t range = static_cast<int16_t>(len * c.expr.range);

    auto noteFor = [&](int degree) {
        int note = 12 * c.expr.octave + key.root + 12 * (degree / len) + intervals[degree % len] +
                   c.expr.transpose;
        return static_cast<uint8_t>(clampValue<int>(note, 0, 127));
    };

    uint8_t steps[kMaxVoices];

    // Arps: step through the chord's tones in every octave of the range.
    if (c.active.mode == NoteMode::arpUp || c.active.mode == NoteMode::arpUpDown)
    {
        ChordType t = c.expr.chord == ChordType::single ? ChordType::triad : c.expr.chord;
        const uint8_t perOctave = chordSteps(t, steps);
        const int16_t tones = static_cast<int16_t>(perOctave * c.expr.range);
        int16_t& i = c.degree;
        if (i < 0 || i >= tones)
            i = 0;

        // A chord's steps can reach past the octave (a 9th): degrees keep
        // counting up the scale, so that still lands on the right note.
        notes[0] = noteFor(steps[i % perOctave] + len * (i / perOctave));

        if (c.active.mode == NoteMode::arpUp)
            i = static_cast<int16_t>((i + 1) % tones);
        else if (tones > 1)
        {
            if (i + c.degreeDir >= tones || i + c.degreeDir < 0)
                c.degreeDir = static_cast<int8_t>(-c.degreeDir);
            i = static_cast<int16_t>(i + c.degreeDir);
        }
        return 1;
    }

    int16_t& d = c.degree;
    switch (c.active.mode)
    {
        case NoteMode::random: d = out.randomBelow ? out.randomBelow(static_cast<uint8_t>(range)) : 0; break;
        case NoteMode::walk:
        {
            int16_t step = out.randomBelow ? static_cast<int16_t>(out.randomBelow(5)) - 2 : 1;
            d += step;
            // Bounce off the ends of the range.
            if (d < 0)
                d = -d;
            if (d >= range)
                d = static_cast<int16_t>(2 * (range - 1) - d);
            break;
        }
        default: break; // the scale runs play d, then move on below
    }
    if (d < 0)
        d = 0;
    if (d >= range)
        d = static_cast<int16_t>(range - 1);

    const int16_t played = d;

    if (c.active.mode == NoteMode::scaleUp)
    {
        d = static_cast<int16_t>((d + 1) % range);
    }
    else if (c.active.mode == NoteMode::scaleUpDown && range > 1)
    {
        if (d + c.degreeDir >= range || d + c.degreeDir < 0)
            c.degreeDir = static_cast<int8_t>(-c.degreeDir);
        d = static_cast<int16_t>(d + c.degreeDir);
    }

    // A chord stacks scale steps on the chosen note; chord tones may reach
    // above the range.
    const uint8_t count = rollChord(c) ? chordSteps(c.expr.chord, steps) : 1;
    if (count == 1)
        steps[0] = 0;
    for (uint8_t k = 0; k < count; k++)
        notes[k] = noteFor(played + steps[k]);
    return count;
}

Key Engine::seqKeyFor(const Expression& x, const Rhythm& r, const Key& global) const
{
    Key k = global;
    switch (x.seqKeyMode)
    {
        case KeyMode::offset: k.root = static_cast<uint8_t>((global.root + x.keyOffset) % 12); break;
        case KeyMode::own:
            k.root = r.homeRoot;
            k.scale = r.homeScale;
            break;
        default: break;
    }
    return k;
}

namespace
{
bool noteInScale(int note, const Key& k)
{
    const uint16_t mask = scaleDef[k.scale <= MAX_SCALE ? k.scale : 0] | 1u;
    return (mask >> ((note - k.root + 120) % 12)) & 1u;
}

// The nearest note in the key; on a tie, the lower one.
int snapToKey(int note, const Key& k)
{
    for (int d = 0; d < 12; d++)
    {
        if (noteInScale(note - d, k))
            return note - d;
        if (noteInScale(note + d, k))
            return note + d;
    }
    return note;
}
} // namespace

// A SEQ list note as played: moved from the list's home key to the key the
// channel follows (the shorter way round, so a melody moves at most a
// tritone), snapped into that key's scale, then transposed.
uint8_t Engine::seqNote(const Channel& c, uint8_t listNote) const
{
    int note = listNote;
    if (c.expr.seqKeyMode != KeyMode::own)
    {
        const Key target = seqKeyFor(c.expr, c.active, globalActive);
        int shift = (target.root - c.active.homeRoot + 12) % 12;
        if (shift > 6)
            shift -= 12;
        note = snapToKey(note + shift, target);
    }
    note += c.expr.transpose;
    return static_cast<uint8_t>(clampValue<int>(note, 0, 127));
}

bool Engine::rollChord(const Channel& c)
{
    if (c.expr.chord == ChordType::single)
        return false;
    if (c.expr.chordChance >= 100)
        return true;
    uint8_t roll = out.randomPercent ? out.randomPercent() : 0;
    return roll < c.expr.chordChance;
}

namespace
{
// `steps` scale notes above `note` in the key (if `note` isn't in the scale
// the count starts from it anyway).
int stepUpInKey(int note, uint8_t steps, const Key& k)
{
    for (uint8_t s = 0; s < steps; s++)
    {
        do
            note++;
        while (note < 127 && !noteInScale(note, k));
    }
    return note;
}
} // namespace

uint8_t Engine::nextSeqNotes(Channel& c, uint8_t* notes)
{
    const Rhythm& r = c.active;
    const uint8_t count = entryCount(r);

    if (c.noteIndex >= count)
        c.noteIndex = 0;
    const uint8_t entry = c.noteIndex;

    if (count > 1)
    {
        switch (r.direction)
        {
            case NoteDirection::forward: c.noteIndex = (c.noteIndex + 1) % count; break;
            case NoteDirection::backward:
                c.noteIndex = c.noteIndex == 0 ? count - 1 : c.noteIndex - 1;
                break;
            case NoteDirection::pingPong:
            {
                int next = c.noteIndex + c.pingPongDir;
                if (next >= count || next < 0)
                {
                    c.pingPongDir = -c.pingPongDir;
                    next = c.noteIndex + c.pingPongDir;
                }
                c.noteIndex = static_cast<uint8_t>(next);
                break;
            }
            case NoteDirection::random:
                c.noteIndex = out.randomBelow ? out.randomBelow(count) : 0;
                break;
            default: c.noteIndex = 0; break;
        }
    }

    const uint8_t start = entryStart(r, entry);
    const uint8_t length = entryLength(r, entry);

    // An entry entered as a chord plays as entered.
    if (length > 1)
    {
        uint8_t n = 0;
        for (uint8_t i = 0; i < length && n < kMaxVoices; i++)
            notes[n++] = seqNote(c, r.notes[start + i]);
        return n;
    }

    // A single note can have the Chord setting stacked on it, in the key
    // the channel plays in.
    notes[0] = seqNote(c, r.notes[start]);
    if (!rollChord(c))
        return 1;

    const Key key = seqKeyFor(c.expr, r, globalActive);
    uint8_t steps[kMaxVoices];
    const uint8_t tones = chordSteps(c.expr.chord, steps);
    for (uint8_t k = 1; k < tones; k++)
        notes[k] = static_cast<uint8_t>(clampValue<int>(stepUpInKey(notes[0], steps[k], key), 0, 127));
    return tones;
}

uint8_t Engine::nextNotes(Channel& c, uint8_t* notes)
{
    if (c.active.mode == NoteMode::seq)
        return nextSeqNotes(c, notes);
    return nextGeneratedNotes(c, notes);
}

void Engine::triggerStep(Channel& c)
{
    const Rhythm& r = c.active;
    const Shift& sh = c.expr.shift;
    const bool pulse = stepIsPulse(r, c.step);

    // Which Shift step applies: per step it moves on every step, played or
    // not; per pulse only on pulses (muted or skipped ones too, like notes);
    // per cycle on the rhythm's first step, and holds for the whole cycle.
    int8_t shiftAmount = 0;

    // The other lanes move the same way, each on its own; a division lane
    // picks the step length for the cycle on its first step.
    int8_t laneValue[kLaneKinds] = { kLaneNone, kLaneNone, kLaneNone, kLaneNone };
    for (int k = 0; k < kLaneKinds; k++)
    {
        const Lane& l = c.expr.lanes[k];
        if (!l.on)
        {
            c.lanePlayed[k] = -1;
            continue;
        }
        const bool perCycle = l.advance == ShiftAdvance::perCycle || k == static_cast<int>(LaneKind::division);
        const bool advance = (perCycle && c.step == 0) || (!perCycle && (l.advance == ShiftAdvance::perStep || pulse));
        if (advance)
        {
            c.laneIndex[k] = static_cast<uint8_t>(c.laneCounter[k] % l.length);
            c.laneCounter[k]++;
        }
        const uint8_t index = static_cast<uint8_t>(c.laneIndex[k] % l.length);
        if (k == static_cast<int>(LaneKind::division))
        {
            if (advance)
            {
                laneValue[k] = l.step[index];
                c.lanePlayed[k] = static_cast<int8_t>(index);
            }
        }
        else if (pulse && (advance || perCycle))
        {
            laneValue[k] = l.step[index];
            c.lanePlayed[k] = static_cast<int8_t>(index);
        }
    }
    {
        const Lane& dl = c.expr.lanes[static_cast<int>(LaneKind::division)];
        if (!dl.on)
            c.stepDivision = r.division;
        else if (c.step == 0)
            c.stepDivision = laneValue[static_cast<int>(LaneKind::division)] >= 0
                               ? static_cast<Division>(laneValue[static_cast<int>(LaneKind::division)])
                               : r.division;
    }

    if (sh.on)
    {
        const bool advance = sh.advance == ShiftAdvance::perStep || (sh.advance == ShiftAdvance::perPulse && pulse) ||
                             (sh.advance == ShiftAdvance::perCycle && c.step == 0);
        if (advance)
        {
            c.shiftIndex = static_cast<uint8_t>(c.shiftCounter % sh.length);
            c.shiftCounter++;
        }
        if (pulse && (advance || sh.advance == ShiftAdvance::perCycle))
        {
            const uint8_t index = static_cast<uint8_t>(c.shiftIndex % sh.length);
            shiftAmount = sh.step[index];
            c.shiftPlayed = static_cast<int8_t>(index);
        }
    }
    if (!pulse)
        return;

    // This step's mode, order and chord from the lanes, for this note only:
    // the channel's own settings come back when the step is done.
    struct LaneOverride
    {
        Channel& c;
        NoteMode mode;
        NoteDirection direction;
        ChordType chord;
        explicit LaneOverride(Channel& ch) : c(ch), mode(ch.active.mode), direction(ch.active.direction), chord(ch.expr.chord) {}
        ~LaneOverride()
        {
            c.active.mode = mode;
            c.active.direction = direction;
            c.expr.chord = chord;
        }
    } restore(c);
    if (laneValue[static_cast<int>(LaneKind::mode)] >= 0)
        c.active.mode = static_cast<NoteMode>(laneValue[static_cast<int>(LaneKind::mode)]);
    if (laneValue[static_cast<int>(LaneKind::order)] >= 0)
        c.active.direction = static_cast<NoteDirection>(laneValue[static_cast<int>(LaneKind::order)]);
    if (laneValue[static_cast<int>(LaneKind::chord)] >= 0)
        c.expr.chord = static_cast<ChordType>(laneValue[static_cast<int>(LaneKind::chord)]);

    // The note sequence advances on every pulse, even muted or skipped ones,
    // so muting a channel doesn't shift where its melody lands.
    uint8_t notes[kMaxVoices];
    uint8_t count = nextNotes(c, notes);

    if (!c.expr.enabled || c.expr.muted || !outputEnabled)
        return;

    // Modulators add to velocity, gate, chance and pitch (their values are
    // worked out once per micro-step in updateMods).
    int velocity = c.expr.velocity;
    int gatePercent = c.expr.gate;
    int chance = c.expr.probability;
    int pitch = 0;
    for (int k = 0; k < kModsPerChannel; k++)
    {
        const int v = c.modState[k].value;    // -1000..1000
        const int a = c.modEffectiveAmount[k]; // percent
        switch (c.expr.mod[k].dest)
        {
            case ModDest::velocity: velocity += a * 127 * v / 100000; break;
            case ModDest::gate: gatePercent += a * v / 1000; break;
            case ModDest::chance: chance += a * v / 1000; break;
            case ModDest::pitch:
            {
                int semis = a * 24 * v;
                pitch += (semis + (semis >= 0 ? 50000 : -50000)) / 100000;
                break;
            }
            default: break;
        }
    }
    velocity = clampValue<int>(velocity, 1, 127);
    gatePercent = clampValue<int>(gatePercent, 1, 100);
    chance = clampValue<int>(chance, 0, 100);
    if (shiftAmount != 0)
        for (uint8_t i = 0; i < count; i++)
            notes[i] = static_cast<uint8_t>(clampValue<int>(shiftNote(c, notes[i], shiftAmount), 0, 127));
    if (pitch != 0)
        for (uint8_t i = 0; i < count; i++)
            notes[i] = static_cast<uint8_t>(clampValue<int>(notes[i] + pitch, 0, 127));

    if (chance < 100)
    {
        uint8_t roll = out.randomPercent ? out.randomPercent() : 0;
        if (roll >= chance)
            return;
    }

    closeNote(c);

    uint16_t stepLength = divisionToMicroSteps(c.stepDivision);
    uint16_t gate = static_cast<uint16_t>((static_cast<uint32_t>(stepLength) * gatePercent) / 100);
    if (gate < 1)
        gate = 1;

    c.openMidiChannel = c.expr.midiChannel;
    c.openSampler = c.expr.output == ChannelOutput::sampler;
    if (c.openSampler)
    {
        // One voice a track: the chord's first note.
        if (count > 0)
        {
            if (out.sampleOn)
                out.sampleOn(static_cast<uint8_t>(&c - channels), c.expr.instrument, notes[0],
                             static_cast<uint8_t>(velocity));
            c.openNotes[c.openCount++] = notes[0];
            c.lastNotes[0] = notes[0];
            c.lastCount = 1;
            c.notesPlayed++;
        }
        count = 0;
    }
    for (uint8_t i = 0; i < count; i++)
    {
        // The same note twice in a chord would cut itself off.
        bool repeat = false;
        for (uint8_t j = 0; j < c.openCount; j++)
            repeat |= c.openNotes[j] == notes[i];
        if (repeat)
            continue;
        if (out.noteOn)
            out.noteOn(notes[i], static_cast<uint8_t>(velocity), c.expr.midiChannel);
        c.openNotes[c.openCount++] = notes[i];
        if (i == 0)
        {
            c.lastCount = 0;
            c.notesPlayed++;
        }
        c.lastNotes[c.lastCount++] = notes[i];

        if (captureBuffer != nullptr && captureCapacity > 0)
        {
            NoteEvent& e = captureBuffer[captureCount % captureCapacity];
            e.time = playTime;
            e.gate = gate;
            e.channel = static_cast<uint8_t>(&c - channels);
            e.note = notes[i];
            e.velocity = static_cast<uint8_t>(velocity);
            e.midiChannel = c.expr.midiChannel;
            captureCount = captureCount + 1;
        }
    }
    c.gateRemaining = gate;
}

//------------------------------------------------------------------------------
// Modulators

uint8_t Engine::modRotationAt(const Mod& m, uint32_t t) const
{
    uint32_t rot = m.rotation;
    if (m.autoRotate > 0)
        rot += t / (static_cast<uint32_t>(m.autoRotate) * kMicroStepsPerBar);
    return static_cast<uint8_t>(rot % m.steps);
}

int Engine::randomLevel()
{
    return out.randomBelow ? (static_cast<int>(out.randomBelow(201)) - 100) * 10 : 0;
}

namespace
{
uint32_t phaseOffset(const Mod& m)
{
    return static_cast<uint32_t>((static_cast<uint64_t>(m.phase) << 32) / 100);
}
} // namespace

void Engine::resetMods(Channel& c)
{
    for (int k = 0; k < kModsPerChannel; k++)
    {
        auto& st = c.modState[k];
        st = Channel::ModState();
        st.phase = phaseOffset(c.expr.mod[k]);
        st.to = static_cast<int16_t>(randomLevel());
        st.from = st.to;
        c.lastCc[k] = -1;
        c.modEffectiveAmount[k] = c.expr.mod[k].amount;
    }
}

// Once per micro-step: moves every modulator on, works out its value, and
// sends CCs.
void Engine::updateMods(Channel& c)
{
    // Modulators aimed at other modulators, from their last values: a
    // depth from 0 to twice the amount set, a speed from half to double.
    int depth[kModsPerChannel];
    int speed[kModsPerChannel];
    for (int k = 0; k < kModsPerChannel; k++)
    {
        depth[k] = 1000;
        speed[k] = 1000;
    }
    for (int k = 0; k < kModsPerChannel; k++)
    {
        const Mod& m = c.expr.mod[k];
        if ((m.dest != ModDest::modAmount && m.dest != ModDest::modRate) || m.target == k)
            continue;
        const int amount = m.amount * c.modState[k].value / 100; // -1000..1000
        if (m.dest == ModDest::modAmount)
            depth[m.target] += amount;
        else
            speed[m.target] = amount >= 0 ? speed[m.target] * (1000 + amount) / 1000
                                          : speed[m.target] * (1000 + amount / 2) / 1000;
    }

    for (int k = 0; k < kModsPerChannel; k++)
    {
        const Mod& m = c.expr.mod[k];
        auto& st = c.modState[k];
        c.modEffectiveAmount[k] =
          static_cast<int16_t>(m.amount * clampValue<int>(depth[k], 0, 2000) / 1000);
        if (m.dest == ModDest::off)
        {
            st.value = 0;
            continue;
        }

        if (isEuclidShape(m.shape))
        {
            const uint32_t stepLen = divisionToMicroSteps(m.division);
            const uint32_t step = playTime / stepLen;
            const uint32_t into = playTime % stepLen;
            const uint8_t rot = modRotationAt(m, playTime);
            const uint8_t index = static_cast<uint8_t>(step % m.steps);
            const bool pulse = isPulse(m.steps, m.pulses, rot, index);

            if (m.shape == ModShape::euclidAccent)
            {
                st.value = pulse ? 1000 : 0;
            }
            else if (m.shape == ModShape::euclidRandom)
            {
                if (step != st.lastStep && pulse)
                    st.value = static_cast<int16_t>(out.randomBelow ? out.randomBelow(101) * 10 : 0);
            }
            else
            {
                // The latest pulse within the decay, counting back.
                const uint32_t decay = static_cast<uint32_t>(m.length) * stepLen;
                st.value = 0;
                for (uint32_t back = 0; back < m.length && back <= step; back++)
                {
                    uint8_t i = static_cast<uint8_t>((step - back) % m.steps);
                    if (isPulse(m.steps, m.pulses, rot, i))
                    {
                        uint32_t since = back * stepLen + into;
                        st.value = static_cast<int16_t>(1000 * (decay - since) / decay);
                        break;
                    }
                }
            }
            st.lastStep = step;
            continue;
        }

        // Smooth shapes: one cycle per `rate`. At normal speed the position
        // comes straight from the bar grid, so it never drifts; when another
        // modulator changes the speed it's moved on step by step instead.
        const uint32_t period = modRateToMicroSteps(m.rate);
        uint32_t cycle;
        if (speed[k] == 1000)
        {
            uint64_t pos = static_cast<uint64_t>(playTime % period) << 32;
            st.phase = phaseOffset(m) + static_cast<uint32_t>(pos / period);
            cycle = playTime / period;
            // The offset can push the position past the end of a cycle.
            if (st.phase < phaseOffset(m))
                cycle++;
        }
        else
        {
            const uint32_t step =
              static_cast<uint32_t>(((static_cast<uint64_t>(1) << 32) / period) * speed[k] / 1000);
            const uint32_t before = st.phase;
            st.phase += step;
            cycle = st.cycle + (st.phase < before ? 1 : 0);
        }

        if (cycle != st.cycle)
        {
            st.cycle = cycle;
            st.from = st.to;
            if (m.shape == ModShape::drift)
            {
                int next = st.to + (out.randomBelow ? (static_cast<int>(out.randomBelow(81)) - 40) * 10 : 0);
                st.to = static_cast<int16_t>(clampValue<int>(next, -1000, 1000));
            }
            else
            {
                st.to = static_cast<int16_t>(randomLevel());
            }
        }

        const float f = static_cast<float>(st.phase) / 4294967296.0f; // 0..1
        float v = 0.0f;
        switch (m.shape)
        {
            case ModShape::sine: v = sinf(6.2831853f * f); break;
            case ModShape::triangle: v = f < 0.25f ? 4.0f * f : (f < 0.75f ? 2.0f - 4.0f * f : 4.0f * f - 4.0f); break;
            case ModShape::sawUp: v = 2.0f * f - 1.0f; break;
            case ModShape::sawDown: v = 1.0f - 2.0f * f; break;
            case ModShape::square: v = f < 0.5f ? 1.0f : -1.0f; break;
            case ModShape::randomSteps: v = st.to / 1000.0f; break;
            case ModShape::smoothRandom:
            case ModShape::drift:
            {
                float glide = 0.5f - 0.5f * cosf(3.14159265f * f);
                v = (st.from + (st.to - st.from) * glide) / 1000.0f;
                break;
            }
            default: break;
        }
        st.value = static_cast<int16_t>(v * 1000.0f);
    }

    // CCs: modulators on the same CC number add up. Smooth shapes swing
    // around the middle (64), Euclidean ones push up from 0 (or down from
    // 127 with a negative amount). Sent only when the value changes, at most
    // every half 1/16 step so they don't crowd the MIDI port.
    if (playTime % 24 != 0 || !c.expr.enabled || c.expr.muted || !outputEnabled)
        return;
    for (int k = 0; k < kModsPerChannel; k++)
    {
        const Mod& m = c.expr.mod[k];
        if (m.dest != ModDest::cc)
            continue;
        bool first = true;
        for (int j = 0; j < k; j++)
            if (c.expr.mod[j].dest == ModDest::cc && c.expr.mod[j].cc == m.cc)
                first = false;
        if (!first)
            continue;

        bool anySmooth = false;
        int sum = 0;
        for (int j = k; j < kModsPerChannel; j++)
        {
            const Mod& o = c.expr.mod[j];
            if (o.dest != ModDest::cc || o.cc != m.cc)
                continue;
            const int a = c.modEffectiveAmount[j];
            const int v = c.modState[j].value;
            if (isEuclidShape(o.shape))
                sum += a * 127 * v / 100000;
            else
            {
                anySmooth = true;
                sum += a * 63 * v / 100000;
            }
        }
        const int base = anySmooth ? 64 : (c.modEffectiveAmount[k] < 0 ? 127 : 0);
        const int value = clampValue<int>(base + sum, 0, 127);
        if (value != c.lastCc[k])
        {
            c.lastCc[k] = static_cast<int16_t>(value);
            if (out.controlChange && c.expr.output == ChannelOutput::midi)
                out.controlChange(m.cc, static_cast<uint8_t>(value), c.expr.midiChannel);
        }
    }
}

int16_t Engine::getModValue(int ch, int k) const
{
    if (!validChannel(ch) || k < 0 || k >= kModsPerChannel)
        return 0;
    return channels[ch].modState[k].value;
}

uint8_t Engine::getModStep(int ch, int k) const
{
    if (!validChannel(ch) || k < 0 || k >= kModsPerChannel)
        return 0;
    const Mod& m = channels[ch].stagedExpr.mod[k];
    return static_cast<uint8_t>((playTime / divisionToMicroSteps(m.division)) % m.steps);
}

uint8_t Engine::getModRotation(int ch, int k) const
{
    if (!validChannel(ch) || k < 0 || k >= kModsPerChannel)
        return 0;
    return modRotationAt(channels[ch].stagedExpr.mod[k], playTime);
}

// A note moved by a Shift step: semitones, or scale steps in the key the
// channel plays in.
int Engine::shiftNote(const Channel& c, int note, int8_t amount) const
{
    if (c.expr.shift.units == ShiftUnits::semitones)
        return note + amount;

    const Key key = c.active.mode == NoteMode::seq ? seqKeyFor(c.expr, c.active, globalActive)
                                                   : keyFor(c.expr, globalActive);
    const int dir = amount > 0 ? 1 : -1;
    for (int n = amount > 0 ? amount : -amount; n > 0; n--)
    {
        do
            note += dir;
        while (note > 0 && note < 127 && !noteInScale(note, key));
    }
    return note;
}

int8_t Engine::getShiftStep(int ch) const
{
    return validChannel(ch) ? channels[ch].shiftPlayed : -1;
}

uint8_t Engine::getLastNotes(int ch, uint8_t* notes) const
{
    if (!validChannel(ch))
        return 0;
    const Channel& c = channels[ch];
    for (uint8_t i = 0; i < c.lastCount; i++)
        notes[i] = c.lastNotes[i];
    return c.lastCount;
}

int8_t Engine::getShiftAmount(int ch) const
{
    if (!validChannel(ch) || !running)
        return 0;
    const Channel& c = channels[ch];
    if (!c.expr.shift.on || c.shiftPlayed < 0 || c.shiftPlayed >= kShiftSteps)
        return 0;
    return c.expr.shift.step[c.shiftPlayed];
}

uint8_t Engine::shiftedNote(int ch, uint8_t note) const
{
    const int8_t amount = getShiftAmount(ch);
    if (amount == 0)
        return note;
    return static_cast<uint8_t>(clampValue<int>(shiftNote(channels[ch], note, amount), 0, 127));
}

uint32_t Engine::getNoteCount(int ch) const
{
    return validChannel(ch) ? channels[ch].notesPlayed : 0;
}

uint8_t Engine::getNotePool(int ch, uint8_t* notes, uint8_t max) const
{
    if (!validChannel(ch))
        return 0;
    const Channel& c = channels[ch];
    const Rhythm& r = c.active;
    const Expression& x = c.expr;
    if (r.mode == NoteMode::seq)
        return 0;
    const Key key = keyFor(x, globalActive);
    uint8_t intervals[12];
    const uint8_t len = scaleIntervals(key, intervals);
    auto noteFor = [&](int degree) {
        int note = 12 * x.octave + key.root + 12 * (degree / len) + intervals[degree % len] + x.transpose;
        return static_cast<uint8_t>(clampValue<int>(note, 0, 127));
    };
    uint8_t n = 0;
    if (r.mode == NoteMode::arpUp || r.mode == NoteMode::arpUpDown)
    {
        uint8_t steps[kMaxVoices];
        const ChordType t = x.chord == ChordType::single ? ChordType::triad : x.chord;
        const uint8_t per = chordSteps(t, steps);
        for (int i = 0; i < per * x.range && n < max; i++)
            notes[n++] = noteFor(steps[i % per] + len * (i / per));
    }
    else
    {
        for (int d = 0; d < len * x.range && n < max; d++)
            notes[n++] = noteFor(d);
    }
    return n;
}

int8_t Engine::getLaneStep(int ch, LaneKind k) const
{
    return validChannel(ch) && k < LaneKind::count ? channels[ch].lanePlayed[static_cast<int>(k)] : -1;
}

void Engine::onMicroStep()
{
    if (!running)
        return;

    const bool launchPoint = launchCounter % launchToMicroSteps(launch) == 0;

    if (launchPoint && globalDirty)
    {
        globalActive = globalPending;
        globalDirty = false;
    }

    for (Channel& c : channels)
    {
        // Gates count down first, so a note that ends exactly where the
        // next one starts is released before the new note-on.
        if (c.openCount > 0)
        {
            if (c.gateRemaining > 0)
                c.gateRemaining--;
            if (c.gateRemaining == 0 || !c.expr.enabled || c.expr.muted)
                closeNote(c);
        }

        if (launchPoint && c.pendingDirty)
            applyPending(c);

        updateMods(c);

        if (c.microStepInStep == 0)
            triggerStep(c);

        c.microStepInStep++;
        if (c.microStepInStep >= divisionToMicroSteps(c.stepDivision))
        {
            c.microStepInStep = 0;
            c.step++;
            if (c.step >= c.active.steps)
                c.step = 0;
        }
    }

    launchCounter++;
    if (launchCounter >= static_cast<uint32_t>(kMicroStepsPerBar) * 4)
        launchCounter = 0;
    playTime++;
}

//------------------------------------------------------------------------------
// Reading settings

Rhythm Engine::getRhythm(int ch) const
{
    if (!validChannel(ch))
        return Rhythm();
    return channels[ch].staged;
}

Expression Engine::getExpression(int ch) const
{
    if (!validChannel(ch))
        return Expression();
    Expression x = channels[ch].stagedExpr;
    x.muted = channels[ch].expr.muted;
    return x;
}

Rhythm Engine::getLiveRhythm(int ch) const
{
    if (!validChannel(ch))
        return Rhythm();
    lock();
    const Channel& c = channels[ch];
    Rhythm r = c.pendingDirty ? c.pending : c.active;
    unlock();
    return r;
}

Expression Engine::getLiveExpression(int ch) const
{
    if (!validChannel(ch))
        return Expression();
    lock();
    const Channel& c = channels[ch];
    Expression x = c.pendingDirty ? c.pendingExpr : c.expr;
    x.muted = c.expr.muted;
    unlock();
    return x;
}

Rhythm Engine::getActiveRhythm(int ch) const
{
    if (!validChannel(ch))
        return Rhythm();
    lock();
    Rhythm r = channels[ch].active;
    unlock();
    return r;
}

bool Engine::hasStagedChange(int ch) const
{
    if (!validChannel(ch))
        return false;
    lock();
    const Channel& c = channels[ch];
    const Rhythm& liveR = c.pendingDirty ? c.pending : c.active;
    const Expression& liveX = c.pendingDirty ? c.pendingExpr : c.expr;
    bool staged = !sameRhythm(c.staged, liveR) || !sameExpression(c.stagedExpr, liveX);
    unlock();
    return staged;
}

bool Engine::hasStagedGlobalKey() const
{
    return !sameKey(globalStaged, getLiveGlobalKey());
}

bool Engine::hasAnyStagedChange() const
{
    if (hasStagedGlobalKey())
        return true;
    for (int ch = 0; ch < kChannels; ch++)
        if (hasStagedChange(ch))
            return true;
    return false;
}

bool Engine::hasPendingChange(int ch) const
{
    return validChannel(ch) && channels[ch].pendingDirty;
}

uint8_t Engine::getCurrentStep(int ch) const
{
    return validChannel(ch) ? channels[ch].step : 0;
}

uint8_t Engine::getNextNoteIndex(int ch) const
{
    return validChannel(ch) ? channels[ch].noteIndex : 0;
}

//------------------------------------------------------------------------------
// Committing

// Sends channel ch's staged settings on. Immediate commits (Shift held)
// play the parts that can change mid-bar straight away; everything else
// waits for the launch point. Call with the lock held.
void Engine::commitLocked(int ch, bool immediate)
{
    Channel& c = channels[ch];
    clampRhythm(c.staged);

    if (!running)
    {
        // Nothing playing, so there's nothing to wait for.
        c.pending = c.staged;
        c.pendingExpr = c.stagedExpr;
        c.pendingExpr.muted = c.expr.muted;
        c.active = c.staged;
        bool muted = c.expr.muted;
        c.expr = c.stagedExpr;
        c.expr.muted = muted;
        c.pendingDirty = false;
        resetChannelPosition(c);
        return;
    }

    c.pending = c.staged;
    c.pendingExpr = c.stagedExpr;

    if (immediate)
    {
        // Notes, velocity, gate, chance, MIDI channel and on/off play now.
        for (uint8_t i = 0; i < kMaxNotes; i++)
            c.active.notes[i] = c.staged.notes[i];
        if (c.active.direction != c.staged.direction)
            c.pingPongDir = 1;
        c.active.noteCount = c.staged.noteCount;
        c.active.direction = c.staged.direction;
        c.active.homeRoot = c.staged.homeRoot;
        c.active.homeScale = c.staged.homeScale;
        c.active.tieMask = c.staged.tieMask;
        if (c.noteIndex >= entryCount(c.active))
            c.noteIndex = 0;

        Expression now = c.stagedExpr;
        copyKeySettings(now, c.expr); // key settings wait for the launch point
        now.muted = c.expr.muted;
        c.expr = now;
    }

    c.pendingDirty = c.pendingMute || rhythmRestarts(c.active, c.pending) || !sameNotes(c.active, c.pending) ||
                     !sameExpression(c.expr, c.pendingExpr) || !sameKeySettings(c.expr, c.pendingExpr);
}

void Engine::commitGlobalLocked()
{
    globalPending = globalStaged;
    if (!running)
    {
        globalActive = globalStaged;
        globalDirty = false;
    }
    else
    {
        globalDirty = !sameKey(globalPending, globalActive);
    }
}

void Engine::commit(int ch)
{
    if (!validChannel(ch))
        return;
    lock();
    commitLocked(ch, false);
    changeCount++;
    unlock();
}

void Engine::commitAll()
{
    lock();
    for (int ch = 0; ch < kChannels; ch++)
        commitLocked(ch, false);
    commitGlobalLocked();
    changeCount++;
    unlock();
}

void Engine::edited(int ch)
{
    changeCount++;
    Channel& c = channels[ch];
    clampRhythm(c.staged);
    c.stagedExpr = clampExpression(c.stagedExpr);
    if (!running || immediateEdits)
        commitLocked(ch, true);
}

void Engine::setOutputEnabled(bool enabled)
{
    lock();
    outputEnabled = enabled;
    if (!enabled)
        for (Channel& c : channels)
            closeNote(c);
    unlock();
}

void Engine::setLayer(bool on)
{
    layer = on;
    changeCount++;
}

void Engine::setFreezeLength(FreezeLength f)
{
    freezeLength = f < FreezeLength::count ? f : FreezeLength::fourBars;
    changeCount++;
}

void Engine::setCaptureBuffer(NoteEvent* buffer, uint32_t capacity)
{
    lock();
    captureBuffer = buffer;
    captureCapacity = buffer != nullptr ? capacity : 0;
    captureCount = 0;
    unlock();
}

uint32_t Engine::captureFirst() const
{
    uint32_t end = captureCount;
    return end > captureCapacity ? end - captureCapacity : 0;
}

namespace
{
uint32_t gcd32(uint32_t a, uint32_t b)
{
    while (b != 0)
    {
        uint32_t t = a % b;
        a = b;
        b = t;
    }
    return a;
}

uint32_t lcm32(uint32_t a, uint32_t b)
{
    if (a == 0 || b == 0)
        return 0;
    return a / gcd32(a, b) * b;
}
} // namespace

uint32_t Engine::repeatLength(int ch) const
{
    if (!validChannel(ch))
        return 0;
    lock();
    const Rhythm r = channels[ch].active;
    const Expression x = channels[ch].expr;
    unlock();

    // Lanes that change the mode, order or step length make the notes too
    // hard to foresee: no repeat. (A chord lane counts like the pitch lane,
    // except on an arp, where the chord is the melody.)
    const bool arp = r.mode == NoteMode::arpUp || r.mode == NoteMode::arpUpDown;
    if (x.lanes[static_cast<int>(LaneKind::mode)].on || x.lanes[static_cast<int>(LaneKind::division)].on ||
        x.lanes[static_cast<int>(LaneKind::order)].on || (x.lanes[static_cast<int>(LaneKind::chord)].on && arp))
        return 0;

    const uint32_t cycle = static_cast<uint32_t>(r.steps) * divisionToMicroSteps(r.division);
    uint32_t pulses = 0;
    for (uint8_t i = 0; i < r.steps; i++)
        pulses += stepIsPulse(r, i);
    if (pulses == 0)
        return cycle;

    // How many pulses the note choice takes to come round again.
    uint32_t notePeriod = 1;
    switch (r.mode)
    {
        case NoteMode::seq:
        {
            uint32_t entries = entryCount(r);
            if (r.direction == NoteDirection::random)
                return 0;
            notePeriod = r.direction == NoteDirection::pingPong && entries > 1 ? 2 * (entries - 1) : entries;
            break;
        }
        case NoteMode::scaleUp:
        case NoteMode::scaleUpDown:
        case NoteMode::arpUp:
        case NoteMode::arpUpDown:
        {
            uint8_t intervals[12];
            uint32_t len = scaleIntervals(keyFor(x, globalActive), intervals);
            uint32_t n = len * x.range;
            if (r.mode == NoteMode::arpUp || r.mode == NoteMode::arpUpDown)
            {
                uint8_t steps[kMaxVoices];
                ChordType t = x.chord == ChordType::single ? ChordType::triad : x.chord;
                n = chordSteps(t, steps) * x.range;
            }
            bool upDown = r.mode == NoteMode::scaleUpDown || r.mode == NoteMode::arpUpDown;
            notePeriod = upDown && n > 1 ? 2 * (n - 1) : n;
            break;
        }
        default: return 0; // Random, Walk
    }

    // The notes line up with the rhythm again after this many cycles.
    uint32_t cycles = lcm32(notePeriod, pulses) / pulses;
    uint32_t period = cycle * cycles;

    // Shift repeats too: per pulse every `length` notes, per step every
    // `length` steps.
    if (x.shift.on)
    {
        if (x.shift.advance == ShiftAdvance::perPulse)
            period = cycle * (lcm32(lcm32(notePeriod, x.shift.length), pulses) / pulses);
        else if (x.shift.advance == ShiftAdvance::perCycle)
            period = lcm32(period, cycle * x.shift.length);
        else
            period = lcm32(period, static_cast<uint32_t>(x.shift.length) * divisionToMicroSteps(r.division));
    }
    const Lane& cl = x.lanes[static_cast<int>(LaneKind::chord)];
    if (cl.on && period != 0)
    {
        if (cl.advance == ShiftAdvance::perPulse)
            period = lcm32(period, cycle * (lcm32(cl.length, pulses) / pulses));
        else if (cl.advance == ShiftAdvance::perCycle)
            period = lcm32(period, cycle * cl.length);
        else
            period = lcm32(period, static_cast<uint32_t>(cl.length) * divisionToMicroSteps(r.division));
    }
    return period;
}

void Engine::setLaunch(Launch l)
{
    if (l >= Launch::count)
        l = Launch::bar;
    launch = l;
    changeCount++;
}

//------------------------------------------------------------------------------
// Edits

void Engine::setRhythm(int ch, const Rhythm& r)
{
    if (!validChannel(ch))
        return;
    lock();
    channels[ch].staged = r;
    edited(ch);
    unlock();
}

void Engine::setExpression(int ch, const Expression& e)
{
    if (!validChannel(ch))
        return;
    lock();
    channels[ch].stagedExpr = e;
    edited(ch);
    // Mute is handled by setMuted, never through staging.
    unlock();
}

#define EUCLID_EDIT(layer, statement)                                                              \
    if (!validChannel(ch))                                                                         \
        return;                                                                                    \
    lock();                                                                                        \
    {                                                                                              \
        auto& v = channels[ch].layer;                                                              \
        statement;                                                                                 \
    }                                                                                              \
    edited(ch);                                                                                    \
    unlock();

void Engine::setSteps(int ch, uint8_t steps)
{
    // A new pattern (even the same one again) replaces the hand changes.
    EUCLID_EDIT(staged, {
        v.steps = clampValue<uint8_t>(steps, 1, kMaxSteps);
        if (v.pulses > v.steps)
            v.pulses = v.steps;
        v.forceOn = v.forceOff = 0;
    })
}
void Engine::setPulses(int ch, uint8_t pulses)
{
    EUCLID_EDIT(staged, {
        v.pulses = pulses;
        v.forceOn = v.forceOff = 0;
    })
}
void Engine::setRotation(int ch, uint8_t rotation)
{
    EUCLID_EDIT(staged, {
        v.rotation = rotation;
        v.forceOn = v.forceOff = 0;
    })
}
void Engine::toggleStep(int ch, uint8_t step)
{
    if (step >= 32)
        return;
    const uint32_t bit = 1u << step;
    EUCLID_EDIT(staged, {
        if (v.forceOn & bit)
            v.forceOn &= ~bit;
        else if (v.forceOff & bit)
            v.forceOff &= ~bit;
        else if (isPulse(v.steps, v.pulses, v.rotation, step))
            v.forceOff |= bit;
        else
            v.forceOn |= bit;
    })
}
void Engine::setDivision(int ch, Division d) { EUCLID_EDIT(staged, v.division = d) }
void Engine::setMode(int ch, NoteMode m) { EUCLID_EDIT(staged, v.mode = m) }
void Engine::setDirection(int ch, NoteDirection d) { EUCLID_EDIT(staged, v.direction = d) }
void Engine::setEnabled(int ch, bool enabled) { EUCLID_EDIT(stagedExpr, v.enabled = enabled) }
void Engine::setMidiChannel(int ch, uint8_t c) { EUCLID_EDIT(stagedExpr, v.midiChannel = c) }
void Engine::setVelocity(int ch, uint8_t velocity) { EUCLID_EDIT(stagedExpr, v.velocity = velocity) }
void Engine::setGate(int ch, uint8_t gate) { EUCLID_EDIT(stagedExpr, v.gate = gate) }
void Engine::setProbability(int ch, uint8_t p) { EUCLID_EDIT(stagedExpr, v.probability = p) }

#undef EUCLID_EDIT

void Engine::setNotes(int ch, const uint8_t* notes, uint8_t count, uint16_t tieMask)
{
    if (!validChannel(ch) || notes == nullptr || count == 0)
        return;
    if (count > kMaxNotes)
        count = kMaxNotes;
    lock();
    Rhythm& r = channels[ch].staged;
    for (uint8_t i = 0; i < count; i++)
        r.notes[i] = notes[i];
    r.noteCount = count;
    r.tieMask = tieMask;
    // The list is entered against the global key as it stands now.
    r.homeRoot = globalStaged.root;
    r.homeScale = globalStaged.scale;
    edited(ch);
    unlock();
}

void Engine::setMuted(int ch, bool muted)
{
    if (!validChannel(ch))
        return;
    lock();
    Channel& c = channels[ch];
    c.expr.muted = muted;
    c.pendingExpr.muted = muted;
    c.stagedExpr.muted = muted;
    changeCount++;
    unlock();
}

uint8_t Engine::getName(int ch) const
{
    return validChannel(ch) ? names[ch] : 0;
}

void Engine::setName(int ch, uint8_t index)
{
    if (!validChannel(ch))
        return;
    names[ch] = index < channelNameCount() ? index : 0;
    textNames[ch][0] = 0;
    changeCount++;
}

const char* Engine::getTextName(int ch) const
{
    return validChannel(ch) ? textNames[ch] : "";
}

void Engine::setTextName(int ch, const char* text)
{
    if (!validChannel(ch))
        return;
    size_t n = 0;
    while (text != nullptr && text[n] != 0 && n < static_cast<size_t>(kTextNameLength))
    {
        const char c = text[n];
        textNames[ch][n] = c >= 32 && c < 127 ? c : ' ';
        n++;
    }
    while (n > 0 && textNames[ch][n - 1] == ' ')
        n--;
    textNames[ch][n] = 0;
    changeCount++;
}

const char* Engine::getNameShown(int ch) const
{
    if (!validChannel(ch))
        return nullptr;
    return textNames[ch][0] != 0 ? textNames[ch] : channelName(names[ch]);
}

void Engine::setMutedAtLaunch(int ch, bool muted)
{
    if (!validChannel(ch))
        return;
    lock();
    Channel& c = channels[ch];
    c.stagedExpr.muted = muted;
    c.pendingExpr.muted = muted;
    if (running)
    {
        if (muted != c.expr.muted)
        {
            c.pendingMute = true;
            c.pendingDirty = true;
        }
    }
    else
    {
        c.expr.muted = muted;
    }
    changeCount++;
    unlock();
}

void Engine::setGlobalKey(const Key& k)
{
    lock();
    globalStaged.root = static_cast<uint8_t>(k.root % 12);
    globalStaged.scale = k.scale <= MAX_SCALE ? k.scale : 0;
    if (!running || immediateEdits)
        commitGlobalLocked();
    changeCount++;
    unlock();
}

// Until there's a UI, the engine boots with a playable demo so the build can
// be heard on external gear: four channels on MIDI channels 1-4.
void loadDemoDefaults(Engine& e)
{
    struct Demo
    {
        uint8_t steps, pulses, rotation;
        Division division;
        uint8_t noteCount;
        uint8_t notes[4];
        uint8_t velocity, gate;
    };

    static const Demo demos[] = {
        {16, 4, 0, Division::d1_16, 1, {36}, 110, 25},             // kick-style four on the floor
        {16, 5, 2, Division::d1_16, 4, {48, 51, 55, 58}, 100, 50}, // bass line, C minor 7
        {8, 3, 0, Division::d1_8, 3, {60, 63, 67}, 90, 70},        // tresillo on 1/8s
        {13, 7, 0, Division::d1_16, 4, {72, 75, 79, 82}, 80, 40},  // 13-step polymeter
    };

    for (int ch = 0; ch < kChannels; ch++)
    {
        Rhythm r;
        Expression x;
        x.midiChannel = static_cast<uint8_t>(ch + 1);

        if (ch < static_cast<int>(sizeof(demos) / sizeof(demos[0])))
        {
            const Demo& d = demos[ch];
            r.steps = d.steps;
            r.pulses = d.pulses;
            r.rotation = d.rotation;
            r.division = d.division;
            r.noteCount = d.noteCount;
            for (uint8_t i = 0; i < d.noteCount; i++)
                r.notes[i] = d.notes[i];
            x.enabled = true;
            x.velocity = d.velocity;
            x.gate = d.gate;
        }

        e.setRhythm(ch, r);
        e.setExpression(ch, x);
    }
}

} // namespace euclid
