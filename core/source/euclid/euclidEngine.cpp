#include "euclidEngine.h"

#include "scales.h"

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
    return !rhythmRestarts(a, b) && sameNotes(a, b);
}

// Mute is left out: it never goes through staging.
bool sameExpression(const Expression& a, const Expression& b)
{
    return a.enabled == b.enabled && a.midiChannel == b.midiChannel && a.velocity == b.velocity &&
           a.gate == b.gate && a.probability == b.probability && a.keyMode == b.keyMode &&
           a.keyOffset == b.keyOffset && a.ownRoot == b.ownRoot && a.ownScale == b.ownScale &&
           a.octave == b.octave && a.range == b.range && a.seqKeyMode == b.seqKeyMode &&
           a.transpose == b.transpose && a.chord == b.chord && a.chordChance == b.chordChance;
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
}

void Engine::closeNote(Channel& c)
{
    if (c.openCount == 0)
        return;
    if (out.noteOff)
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
    bool muted = c.expr.muted;
    c.expr = c.pendingExpr;
    c.expr.muted = muted;
    c.pendingDirty = false;

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
    }
    launchCounter = 0;
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
    if (!isPulse(r.steps, r.pulses, r.rotation, c.step))
        return;

    // The note sequence advances on every pulse, even muted or skipped ones,
    // so muting a channel doesn't shift where its melody lands.
    uint8_t notes[kMaxVoices];
    uint8_t count = nextNotes(c, notes);

    if (!c.expr.enabled || c.expr.muted)
        return;

    if (c.expr.probability < 100)
    {
        uint8_t roll = out.randomPercent ? out.randomPercent() : 0;
        if (roll >= c.expr.probability)
            return;
    }

    closeNote(c);

    uint16_t stepLength = divisionToMicroSteps(r.division);
    uint16_t gate = static_cast<uint16_t>((static_cast<uint32_t>(stepLength) * c.expr.gate) / 100);
    if (gate < 1)
        gate = 1;

    c.openMidiChannel = c.expr.midiChannel;
    for (uint8_t i = 0; i < count; i++)
    {
        // The same note twice in a chord would cut itself off.
        bool repeat = false;
        for (uint8_t j = 0; j < c.openCount; j++)
            repeat |= c.openNotes[j] == notes[i];
        if (repeat)
            continue;
        if (out.noteOn)
            out.noteOn(notes[i], c.expr.velocity, c.expr.midiChannel);
        c.openNotes[c.openCount++] = notes[i];
    }
    c.gateRemaining = gate;
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

        if (c.microStepInStep == 0)
            triggerStep(c);

        c.microStepInStep++;
        if (c.microStepInStep >= divisionToMicroSteps(c.active.division))
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

    c.pendingDirty = rhythmRestarts(c.active, c.pending) || !sameNotes(c.active, c.pending) ||
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
    EUCLID_EDIT(staged, {
        v.steps = clampValue<uint8_t>(steps, 1, kMaxSteps);
        if (v.pulses > v.steps)
            v.pulses = v.steps;
    })
}
void Engine::setPulses(int ch, uint8_t pulses) { EUCLID_EDIT(staged, v.pulses = pulses) }
void Engine::setRotation(int ch, uint8_t rotation) { EUCLID_EDIT(staged, v.rotation = rotation) }
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
    changeCount++;
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
