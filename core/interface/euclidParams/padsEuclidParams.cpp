// Pads on the EUCLID screen: a selector for the setting in focus, lit to
// show the choices. Rows that pick a note (key, roots) light like a piano
// octave in every row of 12 pads. The value playing is bright and a staged
// value that hasn't been committed blinks.

#include "euclidParams/euclidParams.h"

#include "euclid/euclidPlatform.h"
#include "euclid/euclidSets.h"
#include "mtConfig.h"
#include "mtPadBoard.h"
#include "mtPadsBacklight.h"
#include "scales.h"

namespace
{
constexpr uint8_t kPads = 48;
constexpr uint8_t kPadColumns = 12;

bool isWhiteKey(uint8_t column)
{
    // C D E F G A B
    return (0b101010110101 >> column) & 1;
}

int clampInt(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

// 0 = Follow, 1-11 = +N, 12 = Own, for SEQ's key mode or the generator's.
int keyIndex(const euclid::Expression& x, bool seq)
{
    euclid::KeyMode m = seq ? x.seqKeyMode : x.keyMode;
    if (m == euclid::KeyMode::follow)
        return 0;
    if (m == euclid::KeyMode::offset)
        return x.keyOffset;
    return 12;
}
} // namespace

cEuclidParams::PadMap cEuclidParams::padMapFor(uint8_t row) const
{
    if (view == View::mod)
        return modPadMap(row);
    if (isShiftSettings())
        return shiftPadMap(row);
    if (view == View::synth)
        return synthPadMap(row);
    if (view == View::chain || view == View::perform)
        return { PadMap::none, 0, 0, 0 };
    if (view == View::global)
    {
        switch (row)
        {
            case globalRoot: return { PadMap::keyboard, 12, 0, 11 };
            case globalScale: return { PadMap::list, euclid::scaleCount(), 0, 0 };
            case globalLaunch: return { PadMap::list, static_cast<uint8_t>(euclid::Launch::count), 0, 0 };
            default: return { PadMap::none, 0, 0, 0 };
        }
    }

    if (view != View::params && !isNoteSettings())
        return { PadMap::none, 0, 0, 0 };
    row = paramForRow(row);

    euclid::Rhythm r = euclidEngine.getRhythm(selectedChannel);
    switch (row)
    {
        case paramName: return { PadMap::list, euclid::channelNameCount(), 0, 0 }; // pad 1 = no name
        case paramSteps: return { PadMap::list, euclid::kMaxSteps, 1, 0 };
        case paramPulses: return { PadMap::list, static_cast<uint8_t>(r.steps + 1), 0, 0 };
        case paramRotation: return { PadMap::list, r.steps, 0, 0 };
        case paramDivision: return { PadMap::list, static_cast<uint8_t>(euclid::Division::count), 0, 0 };
        case paramMode: return { PadMap::list, static_cast<uint8_t>(euclid::NoteMode::count), 0, 0 };
        case paramChord: return { PadMap::list, static_cast<uint8_t>(euclid::ChordType::count), 0, 0 };
        case paramChordChance: return { PadMap::level, kPads, 0, 100 };
        case paramNoteOrder: return { PadMap::list, static_cast<uint8_t>(euclid::NoteDirection::count), 0, 0 };
        case paramKey: return { PadMap::keyboard, 12, 0, 11 }; // column 0 = Follow, then +1 ... +11
        case paramOwnRoot: return { PadMap::keyboard, 12, 0, 11 };
        case paramOwnScale: return { PadMap::list, euclid::scaleCount(), 0, 0 };
        case paramOctave: return { PadMap::list, 9, 0, 0 };
        case paramRange: return { PadMap::list, 4, 1, 0 };
        case paramTranspose: return { PadMap::list, kPads, -euclid::kMaxTranspose, 0 }; // -24 ... +23
        case paramVelocity: return { PadMap::level, kPads, 1, 127 };
        case paramGate: return { PadMap::level, kPads, 1, 100 };
        case paramChance: return { PadMap::level, kPads, 0, 100 };
        case paramMidiChannel: return { PadMap::list, 16, 1, 0 };

        case paramEnabled: return { PadMap::list, 2, 0, 0 };
        default: return { PadMap::none, 0, 0, 0 };
    }
}

// The row's value as a number, from the staged settings (what the screen
// shows) or the live ones (committed, playing or about to).
int cEuclidParams::rowValue(uint8_t row, bool live) const
{
    if (view == View::mod)
        return modRowValue(row, live);
    if (view == View::shift)
        return shiftSettings ? shiftRowValue(row, live) : kNoValue;
    if (view == View::synth)
        return synthRowValue(row); // not staged: live and staged are the same
    if (view == View::chain || view == View::perform)
        return kNoValue;
    if (view == View::global)
    {
        euclid::Key k = live ? euclidEngine.getLiveGlobalKey() : euclidEngine.getGlobalKey();
        switch (row)
        {
            case globalRoot: return k.root;
            case globalScale: return k.scale;
            case globalLaunch: return static_cast<int>(euclidEngine.getLaunch());
            default: return kNoValue;
        }
    }

    const uint8_t ch = selectedChannel;
    euclid::Rhythm r = live ? euclidEngine.getLiveRhythm(ch) : euclidEngine.getRhythm(ch);
    euclid::Expression x = live ? euclidEngine.getLiveExpression(ch) : euclidEngine.getExpression(ch);

    row = paramForRow(row);
    switch (row)
    {
        case paramName: return euclidEngine.getName(ch); // not staged: live == staged
        case paramSteps: return r.steps;
        case paramPulses: return r.pulses;
        case paramRotation: return r.rotation;
        case paramDivision: return static_cast<int>(r.division);
        case paramMode: return static_cast<int>(r.mode);
        case paramChord: return static_cast<int>(x.chord);
        case paramChordChance: return x.chordChance;
        case paramNoteOrder: return static_cast<int>(r.direction);
        case paramKey: return keyIndex(x, r.mode == euclid::NoteMode::seq);
        case paramOwnRoot: return x.ownRoot;
        case paramOwnScale: return x.ownScale;
        case paramOctave: return x.octave;
        case paramRange: return x.range;
        case paramTranspose: return x.transpose;
        case paramVelocity: return x.velocity;
        case paramGate: return x.gate;
        case paramChance: return x.probability;
        case paramMidiChannel: return x.midiChannel;

        case paramEnabled: return x.enabled ? 1 : 0;
        default: return kNoValue;
    }
}

// Sets a row to an absolute value. Whether it's staged or goes live is up to
// the engine's immediate flag, which the caller sets from Shift.
void cEuclidParams::setRowValue(uint8_t row, int v)
{
    if (view == View::mod)
    {
        setModRowValue(row, v);
        return;
    }
    if (isShiftSettings())
    {
        setShiftRowValue(row, v);
        return;
    }
    if (view == View::synth)
    {
        setSynthRowValue(row, v);
        return;
    }
    if (view == View::global)
    {
        euclid::Key k = euclidEngine.getGlobalKey();
        switch (row)
        {
            case globalRoot:
                k.root = static_cast<uint8_t>(clampInt(v, 0, 11));
                euclidEngine.setGlobalKey(k);
                break;
            case globalScale:
                k.scale = static_cast<uint8_t>(clampInt(v, 0, euclid::scaleCount() - 1));
                euclidEngine.setGlobalKey(k);
                break;
            case globalLaunch:
                euclidEngine.setLaunch(static_cast<euclid::Launch>(
                  clampInt(v, 0, static_cast<int>(euclid::Launch::count) - 1)));
                break;
            default: return;
        }
        refreshGlobal();
        refreshPads();
        return;
    }

    const uint8_t ch = selectedChannel;
    euclid::Rhythm r = euclidEngine.getRhythm(ch);
    euclid::Expression x = euclidEngine.getExpression(ch);

    row = paramForRow(row);
    switch (row)
    {
        case paramName: euclidEngine.setName(ch, static_cast<uint8_t>(clampInt(v, 0, euclid::channelNameCount() - 1))); break;
        case paramSteps: euclidEngine.setSteps(ch, static_cast<uint8_t>(clampInt(v, 1, euclid::kMaxSteps))); break;
        case paramPulses: euclidEngine.setPulses(ch, static_cast<uint8_t>(clampInt(v, 0, r.steps))); break;
        case paramRotation: euclidEngine.setRotation(ch, static_cast<uint8_t>(clampInt(v, 0, r.steps - 1))); break;
        case paramDivision:
            euclidEngine.setDivision(ch, static_cast<euclid::Division>(
                                           clampInt(v, 0, static_cast<int>(euclid::Division::count) - 1)));
            break;
        case paramMode:
            euclidEngine.setMode(ch, static_cast<euclid::NoteMode>(
                                       clampInt(v, 0, static_cast<int>(euclid::NoteMode::count) - 1)));
            break;
        case paramChord:
            x.chord = static_cast<euclid::ChordType>(
              clampInt(v, 0, static_cast<int>(euclid::ChordType::count) - 1));
            euclidEngine.setExpression(ch, x);
            break;
        case paramChordChance:
            x.chordChance = static_cast<uint8_t>(clampInt(v, 0, 100));
            euclidEngine.setExpression(ch, x);
            break;
        case paramNoteOrder:
            euclidEngine.setDirection(ch, static_cast<euclid::NoteDirection>(
                                            clampInt(v, 0, static_cast<int>(euclid::NoteDirection::count) - 1)));
            break;
        case paramKey:
        {
            int k = clampInt(v, 0, 12);
            euclid::KeyMode& m = r.mode == euclid::NoteMode::seq ? x.seqKeyMode : x.keyMode;
            if (k == 0)
                m = euclid::KeyMode::follow;
            else if (k == 12)
                m = euclid::KeyMode::own;
            else
            {
                m = euclid::KeyMode::offset;
                x.keyOffset = static_cast<uint8_t>(k);
            }
            euclidEngine.setExpression(ch, x);
            break;
        }
        case paramOwnRoot:
            x.ownRoot = static_cast<uint8_t>(clampInt(v, 0, 11));
            euclidEngine.setExpression(ch, x);
            break;
        case paramOwnScale:
            x.ownScale = static_cast<uint8_t>(clampInt(v, 0, euclid::scaleCount() - 1));
            euclidEngine.setExpression(ch, x);
            break;
        case paramOctave:
            x.octave = static_cast<uint8_t>(clampInt(v, 0, 8));
            euclidEngine.setExpression(ch, x);
            break;
        case paramRange:
            x.range = static_cast<uint8_t>(clampInt(v, 1, 4));
            euclidEngine.setExpression(ch, x);
            break;
        case paramTranspose:
            x.transpose = static_cast<int8_t>(clampInt(v, -euclid::kMaxTranspose, euclid::kMaxTranspose));
            euclidEngine.setExpression(ch, x);
            break;
        case paramVelocity: euclidEngine.setVelocity(ch, static_cast<uint8_t>(clampInt(v, 1, 127))); break;
        case paramGate: euclidEngine.setGate(ch, static_cast<uint8_t>(clampInt(v, 1, 100))); break;
        case paramChance: euclidEngine.setProbability(ch, static_cast<uint8_t>(clampInt(v, 0, 100))); break;
        case paramMidiChannel: euclidEngine.setMidiChannel(ch, static_cast<uint8_t>(clampInt(v, 1, 16))); break;

        case paramEnabled: euclidEngine.setEnabled(ch, v != 0); break;
        default: return;
    }

    refreshParams();
    refreshChannels();
    refreshRing();
    refreshPads();
}

// Value <-> pad for one row.
int cEuclidParams::valueToPad(const PadMap& m, int v) const
{
    if (v == kNoValue)
        return -1;
    switch (m.kind)
    {
        case PadMap::list: return v - m.minValue;
        case PadMap::keyboard: return v; // column; lit in every row
        case PadMap::level:
            return (v - m.minValue) * (kPads - 1) / (m.maxValue - m.minValue);
        default: return -1;
    }
}

int cEuclidParams::padToValue(const PadMap& m, uint8_t pad) const
{
    switch (m.kind)
    {
        case PadMap::list: return pad < m.count ? pad + m.minValue : kNoValue;
        case PadMap::keyboard: return pad % kPadColumns;
        case PadMap::level:
            return m.minValue + (pad * (m.maxValue - m.minValue) + (kPads - 1) / 2) / (kPads - 1);
        default: return kNoValue;
    }
}

void cEuclidParams::stopPadBlinks()
{
    for (uint8_t pad = 0; pad < kPads; pad++)
    {
        if (blinkingPads & (1ull << pad))
            padsBacklight.stopBlink(pad);
    }
    blinkingPads = 0;
}

void cEuclidParams::blinkPad(uint8_t pad, uint8_t level)
{
    if (blinkingPads & (1ull << pad))
        return;
    padsBacklight.startBlink(level, pad);
    blinkingPads |= 1ull << pad;
}

void cEuclidParams::refreshPads()
{
    const uint8_t dim = mtConfig.values.padsLightBackDim;
    const uint8_t mid = mtConfig.values.padsLightBack;
    const uint8_t bright = mtConfig.values.padsLightFront;

    stopPadBlinks();
    for (uint8_t pad = 0; pad < kPads; pad++)
        padsBacklight.setBackLayer(0, 0, pad);

    if (view == View::lines)
    {
        // The selected channel's steps, a pad each: pulses bright, rests dim,
        // steps changed by hand in between.
        const euclid::Rhythm r = euclidEngine.getRhythm(selectedChannel);
        for (uint8_t pad = 0; pad < r.steps && pad < kPads; pad++)
        {
            const bool byHand = pad < 32 && (((r.forceOn | r.forceOff) >> pad) & 1u);
            if (byHand)
                padsBacklight.setBackLayer(1, mid, pad);
            else
                padsBacklight.setBackLayer(1, euclid::stepIsPulse(r, pad) ? bright : dim, pad);
        }
        return;
    }

    if (view == View::notes && !notesSettings)
    {
        // The channel's key on the stock pad layout: notes in the key glow,
        // its root a little brighter.
        euclid::Key k = euclidEngine.getChannelKey(selectedChannel);
        for (uint8_t pad = 0; pad < kPads; pad++)
        {
            uint8_t note = mtPadBoard.getNoteFromPad(pad);
            if (note % 12 == k.root)
                padsBacklight.setBackLayer(1, mid, pad);
            else if (isInScale(note, k.root, k.scale))
                padsBacklight.setBackLayer(1, dim, pad);
        }
        return;
    }

    if (view == View::shift && !shiftSettings)
    {
        // The lane: steps in use glow, steps with a shift a little more,
        // the cursor brightest.
        const uint8_t length = shiftLaneLength();
        for (uint8_t pad = 0; pad < length && pad < kPads; pad++)
        {
            if (pad == shiftCursor)
                padsBacklight.setBackLayer(1, bright, pad);
            else if (shiftStepUsed(pad))
                padsBacklight.setBackLayer(1, mid, pad);
            else
                padsBacklight.setBackLayer(1, dim, pad);
        }
        return;
    }

    if (view == View::sets)
    {
        for (uint8_t slot = 0; slot < euclid::kSetSlots; slot++)
        {
            if (slot == setCursor)
                padsBacklight.setBackLayer(1, bright, slot);
            else if (euclidSets::exists(slot + 1))
                padsBacklight.setBackLayer(1, mid, slot);
            else
                padsBacklight.setBackLayer(1, dim, slot);
        }
        return;
    }

    const uint8_t row = focusedRow();
    const PadMap m = padMapFor(row);
    if (m.kind == PadMap::none)
        return;

    const int livePad = valueToPad(m, rowValue(row, true));
    const int stagedPad = valueToPad(m, rowValue(row, false));

    if (m.kind == PadMap::keyboard)
    {
        for (uint8_t pad = 0; pad < kPads; pad++)
        {
            int column = pad % kPadColumns;
            if (column == livePad)
                padsBacklight.setBackLayer(1, bright, pad);
            else if (isWhiteKey(static_cast<uint8_t>(column)))
                padsBacklight.setBackLayer(1, dim, pad);
            if (column == stagedPad && stagedPad != livePad)
                blinkPad(pad, bright);
        }
        return;
    }

    for (uint8_t pad = 0; pad < kPads && pad < m.count; pad++)
    {
        bool lit = m.kind == PadMap::level ? pad <= livePad : true;
        if (pad == livePad)
            padsBacklight.setBackLayer(1, bright, pad);
        else if (lit)
            padsBacklight.setBackLayer(1, dim, pad);
    }
    if (stagedPad >= 0 && stagedPad < kPads && stagedPad != livePad)
        blinkPad(static_cast<uint8_t>(stagedPad), bright);
}

// Returns true if the pad picked a value (so it shouldn't play a note).
bool cEuclidParams::padSelect(uint8_t pad)
{
    if (view == View::sets)
    {
        if (pad < euclid::kSetSlots)
            moveSetCursor(static_cast<int8_t>(pad - setCursor));
        return true;
    }

    if (view == View::shift && !shiftSettings)
    {
        // A pad picks the step to shift.
        moveShiftCursor(static_cast<int8_t>(pad - shiftCursor));
        return true;
    }

    if (!isListView())
        return false;

    const uint8_t row = focusedRow();
    const PadMap m = padMapFor(row);
    if (m.kind == PadMap::none)
        return false;

    int v = padToValue(m, pad);
    if (v != kNoValue)
        setRowValue(row, v);
    return true;
}
