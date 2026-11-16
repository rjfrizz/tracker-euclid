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

const char* noteModeName(NoteMode m)
{
    switch (m)
    {
        case NoteMode::seq: return "SEQ";
        case NoteMode::random: return "Random";
        case NoteMode::walk: return "Walk";
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
    barMicroStep = 0;
    for (Channel& c : channels)
    {
        c = Channel();
    }
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
}

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
    if (!c.noteOpen)
        return;
    if (out.noteOff)
        out.noteOff(c.openNote, c.openMidiChannel);
    c.noteOpen = false;
    c.gateRemaining = 0;
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
        {
            c.active = c.pending;
            c.pendingDirty = false;
        }
        resetChannelPosition(c);
    }
    barMicroStep = 0;
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

Key Engine::keyFor(const Channel& c, const Key& global) const
{
    Key k = global;
    switch (c.expr.keyMode)
    {
        case KeyMode::offset: k.root = static_cast<uint8_t>((global.root + c.expr.keyOffset) % 12); break;
        case KeyMode::own:
            k.root = c.expr.ownRoot;
            k.scale = c.expr.ownScale;
            break;
        default: break;
    }
    return k;
}

Key Engine::getChannelKey(int ch) const
{
    if (!validChannel(ch))
        return globalPending;
    return keyFor(channels[ch], globalPending);
}

uint8_t Engine::nextGeneratedNote(Channel& c)
{
    const Key key = keyFor(c, globalActive);

    // Semitone offsets of the scale's notes above the root, from the stock
    // 12-bit masks (bit n set = n semitones above the root is in the scale).
    uint8_t intervals[12];
    uint8_t len = 0;
    const uint16_t mask = scaleDef[key.scale <= MAX_SCALE ? key.scale : 0] | 1u;
    for (uint8_t n = 0; n < 12; n++)
        if (mask & (1u << n))
            intervals[len++] = n;

    const int16_t range = static_cast<int16_t>(len * c.expr.range);
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
        case NoteMode::arpUp: break; // plays d, then moves up below
        case NoteMode::arpUpDown: break;
        default: break;
    }
    if (d < 0)
        d = 0;
    if (d >= range)
        d = static_cast<int16_t>(range - 1);

    int note = 12 * c.expr.octave + key.root + 12 * (d / len) + intervals[d % len];

    // The arps play the current degree, then move on.
    if (c.active.mode == NoteMode::arpUp)
    {
        d = static_cast<int16_t>((d + 1) % range);
    }
    else if (c.active.mode == NoteMode::arpUpDown && range > 1)
    {
        if (d + c.degreeDir >= range || d + c.degreeDir < 0)
            c.degreeDir = static_cast<int8_t>(-c.degreeDir);
        d = static_cast<int16_t>(d + c.degreeDir);
    }

    return static_cast<uint8_t>(clampValue<int>(note, 0, 127));
}

uint8_t Engine::nextNote(Channel& c)
{
    if (c.active.mode != NoteMode::seq)
        return nextGeneratedNote(c);

    const Rhythm& r = c.active;
    uint8_t count = r.noteCount;
    if (count <= 1)
        return r.notes[0];

    if (c.noteIndex >= count)
        c.noteIndex = 0;

    uint8_t note = r.notes[c.noteIndex];

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
    return note;
}

void Engine::triggerStep(Channel& c)
{
    const Rhythm& r = c.active;
    if (!isPulse(r.steps, r.pulses, r.rotation, c.step))
        return;

    // The note sequence advances on every pulse, even muted or skipped ones,
    // so muting a channel doesn't shift where its melody lands.
    uint8_t note = nextNote(c);

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

    if (out.noteOn)
        out.noteOn(note, c.expr.velocity, c.expr.midiChannel);
    c.noteOpen = true;
    c.openNote = note;
    c.openMidiChannel = c.expr.midiChannel;
    c.gateRemaining = gate;
}

void Engine::onMicroStep()
{
    if (!running)
        return;

    bool barStart = barMicroStep == 0;

    if (barStart && globalDirty)
    {
        globalActive = globalPending;
        globalDirty = false;
    }

    for (Channel& c : channels)
    {
        // Gates count down first, so a note that ends exactly where the
        // next one starts is released before the new note-on.
        if (c.noteOpen)
        {
            if (c.gateRemaining > 0)
                c.gateRemaining--;
            if (c.gateRemaining == 0 || !c.expr.enabled || c.expr.muted)
                closeNote(c);
        }

        if (barStart && c.pendingDirty)
        {
            c.active = c.pending;
            c.pendingDirty = false;
            closeNote(c);
            resetChannelPosition(c);
        }

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

    barMicroStep++;
    if (barMicroStep >= kMicroStepsPerBar)
        barMicroStep = 0;
}

Rhythm Engine::getRhythm(int ch) const
{
    if (!validChannel(ch))
        return Rhythm();
    lock();
    Rhythm r = channels[ch].pending;
    unlock();
    return r;
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

Expression Engine::getExpression(int ch) const
{
    if (!validChannel(ch))
        return Expression();
    return channels[ch].expr;
}

bool Engine::hasPendingChange(int ch) const
{
    return validChannel(ch) && channels[ch].pendingDirty;
}

uint8_t Engine::getCurrentStep(int ch) const
{
    return validChannel(ch) ? channels[ch].step : 0;
}

void Engine::markDirty(int ch)
{
    changeCount++;
    Channel& c = channels[ch];
    clampRhythm(c.pending);
    // While stopped there's no bar to wait for.
    if (!running)
    {
        c.active = c.pending;
        c.pendingDirty = false;
        resetChannelPosition(c);
    }
    else
    {
        c.pendingDirty = true;
    }
}

void Engine::setRhythm(int ch, const Rhythm& r)
{
    if (!validChannel(ch))
        return;
    lock();
    channels[ch].pending = r;
    markDirty(ch);
    unlock();
}

#define EUCLID_SET_RHYTHM(field, value)                                                            \
    if (!validChannel(ch))                                                                         \
        return;                                                                                    \
    lock();                                                                                        \
    channels[ch].pending.field = (value);                                                          \
    markDirty(ch);                                                                                 \
    unlock();

void Engine::setSteps(int ch, uint8_t steps)
{
    if (!validChannel(ch))
        return;
    lock();
    Rhythm& p = channels[ch].pending;
    p.steps = clampValue<uint8_t>(steps, 1, kMaxSteps);
    if (p.pulses > p.steps)
        p.pulses = p.steps;
    markDirty(ch);
    unlock();
}

void Engine::setPulses(int ch, uint8_t pulses) { EUCLID_SET_RHYTHM(pulses, pulses) }
void Engine::setRotation(int ch, uint8_t rotation) { EUCLID_SET_RHYTHM(rotation, rotation) }
void Engine::setDivision(int ch, Division d) { EUCLID_SET_RHYTHM(division, d) }

#undef EUCLID_SET_RHYTHM

// Copies the pending notes and order to the playing settings without
// waiting for the bar, keeping the sequence position where it is.
void Engine::applyNotesNow(Channel& c)
{
    for (uint8_t i = 0; i < kMaxNotes; i++)
        c.active.notes[i] = c.pending.notes[i];
    c.active.noteCount = c.pending.noteCount;
    c.active.direction = c.pending.direction;
    if (c.noteIndex >= c.active.noteCount)
        c.noteIndex = 0;
}

void Engine::setNotes(int ch, const uint8_t* notes, uint8_t count)
{
    if (!validChannel(ch) || notes == nullptr || count == 0)
        return;
    if (count > kMaxNotes)
        count = kMaxNotes;
    lock();
    Channel& c = channels[ch];
    for (uint8_t i = 0; i < count; i++)
        c.pending.notes[i] = clampValue<uint8_t>(notes[i], 0, 127);
    c.pending.noteCount = count;
    applyNotesNow(c);
    changeCount++;
    unlock();
}

void Engine::setDirection(int ch, NoteDirection d)
{
    if (!validChannel(ch) || d >= NoteDirection::count)
        return;
    lock();
    Channel& c = channels[ch];
    c.pending.direction = d;
    c.pingPongDir = 1;
    applyNotesNow(c);
    changeCount++;
    unlock();
}

void Engine::setMode(int ch, NoteMode m)
{
    if (!validChannel(ch))
        return;
    lock();
    channels[ch].pending.mode = m;
    markDirty(ch);
    unlock();
}

void Engine::setGlobalKey(const Key& k)
{
    Key x;
    x.root = static_cast<uint8_t>(k.root % 12);
    x.scale = k.scale <= MAX_SCALE ? k.scale : 0;
    lock();
    globalPending = x;
    if (!running)
    {
        globalActive = x;
        globalDirty = false;
    }
    else
    {
        globalDirty = globalPending.root != globalActive.root || globalPending.scale != globalActive.scale;
    }
    changeCount++;
    unlock();
}

uint8_t Engine::getNextNoteIndex(int ch) const
{
    return validChannel(ch) ? channels[ch].noteIndex : 0;
}

void Engine::setExpression(int ch, const Expression& e)
{
    if (!validChannel(ch))
        return;
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
    lock();
    channels[ch].expr = x;
    changeCount++;
    unlock();
}

void Engine::setEnabled(int ch, bool enabled)
{
    Expression e = getExpression(ch);
    e.enabled = enabled;
    setExpression(ch, e);
}

void Engine::setMuted(int ch, bool muted)
{
    Expression e = getExpression(ch);
    e.muted = muted;
    setExpression(ch, e);
}

void Engine::setMidiChannel(int ch, uint8_t midiChannel)
{
    Expression e = getExpression(ch);
    e.midiChannel = midiChannel;
    setExpression(ch, e);
}

void Engine::setVelocity(int ch, uint8_t velocity)
{
    Expression e = getExpression(ch);
    e.velocity = velocity;
    setExpression(ch, e);
}

void Engine::setGate(int ch, uint8_t gate)
{
    Expression e = getExpression(ch);
    e.gate = gate;
    setExpression(ch, e);
}

void Engine::setProbability(int ch, uint8_t probability)
{
    Expression e = getExpression(ch);
    e.probability = probability;
    setExpression(ch, e);
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
