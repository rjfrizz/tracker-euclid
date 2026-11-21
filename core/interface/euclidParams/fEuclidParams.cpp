#include "euclidParams/euclidParams.h"

#include "euclid/euclidHarmony.h"
#include "euclid/euclidChain.h"
#include "euclid/euclidPlatform.h"
#include "euclid/euclidSets.h"
#include "Platform.h"
#include "TrackerImpl.h"

#include "mtConfig.h"
#include "mtMidi.h"
#include "mtPadBoard.h"
#include "mtPadsBacklight.h"
#include "mtSequencer.h"
#include "mtStructs.h"
#include "fileManager.h"

#include <stdio.h>
#include <string.h>

cEuclidParams euclidParams;
static cEuclidParams* EP = &euclidParams;

// start() sets moduleRefresh and stop() clears it, so it says whether this
// screen is the one showing.
bool euclidScreenActive()
{
    return euclidParams.moduleRefresh != 0;
}

static void functPlayAction();
static void functUp();
static void functDown();
static void functLeft();
static void functRight();
static void functEncoder(int16_t value);
static void functChannelButton(uint8_t button, uint8_t state);
static void functSwitchModule(uint8_t button);
static void functParamsView();
static void functNotesView();
static void functSetsView();
static void functGlobalView();
static void functModView();
static void functShiftView();
static void functSynthView();
static void functChainView();
static void functLinesView();
static void functDelete();
static void functInsert();
static void functPads(uint8_t pad, uint8_t state, int16_t velo);
static void functRec(uint8_t button, uint8_t state);
static void functScene(uint8_t button);

static constexpr uint8_t kAuditionVelocity = 100;

// Edits made while Shift is held skip staging and go live (see the engine's
// file comment for what that means for rhythm and key changes).
namespace
{
struct ShiftEdits
{
    ShiftEdits() { euclidEngine.setImmediate(EP->buttons->isButtonPressed(interfaceButtonShift)); }
    ~ShiftEdits() { euclidEngine.setImmediate(false); }
};
} // namespace

void cEuclidParams::update()
{
    updateMessage();
    updateHarmony();

    // Settings changed from outside this screen, such as loading a set or
    // the working state restored at boot.
    uint32_t changes = euclidEngine.getChangeCount();
    if (changes != lastChangeCount)
    {
        lastChangeCount = changes;
        if (view == View::params)
            refreshParams();
        else if (view == View::notes)
            refreshNotes();
        else if (view == View::sets)
            refreshSets();
        else if (view == View::mod)
            refreshMod();
        else if (view == View::shift)
            refreshShift();
        else if (view == View::synth)
            refreshSynth();
        else
            refreshGlobal();
        refreshTitle();
        refreshChannels();
        refreshRing();
        refreshPads();
    }

    // Committed changes landing at the launch point change what's "live",
    // which moves the bright pad and clears the arrows.
    uint16_t pendingMask = euclidEngine.hasPendingGlobalKey() ? 0x100 : 0;
    for (uint8_t ch = 0; ch < euclid::kChannels; ch++)
        if (euclidEngine.hasPendingChange(ch))
            pendingMask |= 1u << ch;
    if (pendingMask != lastPendingMask)
    {
        lastPendingMask = pendingMask;
        if (view == View::params)
            refreshParams();
        else if (view == View::global)
            refreshGlobal();
        else if (view == View::mod)
            refreshMod();
        refreshPads();
    }

    // The title says when a whole patch is on its way, either way.
    const bool synthBusy = euclidSynth::sending() || euclidSynth::receiving();
    if (view == View::synth && synthBusy != synthWasSending)
    {
        synthWasSending = synthBusy;
        refreshTitle();
    }

    // A patch arrived from the synth, or it didn't answer.
    if (euclidSynth::receiveEvents() != lastSynthEvents)
    {
        lastSynthEvents = euclidSynth::receiveEvents();
        if (view == View::synth)
        {
            showMessage(euclidSynth::receiveMessage());
            refreshSynth();
            refreshPads();
        }
    }

    // The lines follow the steps being played (checked cheaply: only a
    // change of step redraws them).
    if ((view == View::lines || view == View::global) && linesState() != lastLinesState &&
        TrackerImpl::getInstance()->getPlatform()->getTimer()->millis() - lastLinesMs >= 30)
    {
        if (view == View::lines)
            refreshLines();
        else
            refreshMiniLines();
    }

    // The chain page follows the step and bar being played.
    if (view == View::chain)
    {
        uint32_t bar = 0;
        const int8_t playing = euclidChain::playingStep(&bar, nullptr);
        if (playing != lastChainPlaying || bar != lastChainBar)
        {
            refreshChain();
            refreshTitle();
        }
    }

    // The Shift grid follows the step being played.
    if (view == View::shift && !shiftSettings && shiftPlayedStep() != lastShiftPlayed)
        refreshShift();

    // Auto-rotation moves the modulator's ring; the meters follow the values.
    if (view == View::mod)
    {
        if (euclidEngine.getModRotation(selectedChannel, selectedMod) != lastModRotation)
            refreshRing();
        updateModMeters(false);
    }

    // Tempo can change outside this screen (external clock, other screens),
    // and a queued key change lands on the bar.
    if (view == View::global)
    {
        int tempo = static_cast<int>(sequencer.getCurrentTempo() + 0.5f);
        bool keyPending = euclidEngine.hasPendingGlobalKey();
        if (tempo != lastShownTempo || keyPending != lastKeyPending)
        {
            lastShownTempo = tempo;
            lastKeyPending = keyPending;
            refreshGlobal();
        }
    }

    bool running = euclidEngine.isRunning();
    int8_t playhead =
      running ? static_cast<int8_t>(euclidEngine.getCurrentStep(selectedChannel)) : -1;
    bool pending = euclidEngine.hasPendingChange(selectedChannel);

    if (playhead != lastPlayhead || pending != lastPending || running != lastRunning)
    {
        bool pendingCleared = lastPending && !pending;
        refreshRing();
        if (pendingCleared)
            refreshChannels();
    }

    if (view == View::notes)
    {
        int8_t next = running ? static_cast<int8_t>(euclidEngine.getNextNoteIndex(selectedChannel)) : -1;
        if (next != lastNextNote || euclidEngine.getNoteCount(selectedChannel) != lastNoteCount)
            refreshNotes();
    }
}

void cEuclidParams::start([[maybe_unused]] uint32_t options)
{
    moduleRefresh = 1;

    for (uint8_t i = 0; i < 48; i++)
        heldPadNote[i] = -1;

    ip->setButtonObj(interfaceButtonPerformance, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonConfig, buttonPress, functSwitchModule);
    // The four Sample keys are Scenes A-D here (Rec + key saves one); with
    // Shift they open the stock sample screens.
    ip->setButtonObj(interfaceButtonSampleLoad, buttonPress, functScene);
    ip->setButtonObj(interfaceButtonSamplePlay, buttonPress, functScene);
    ip->setButtonObj(interfaceButtonSampleEdit, buttonPress, functScene);
    ip->setButtonObj(interfaceButtonSampleRec, buttonPress, functScene);
    // There's no stock Pattern or Song screen: both keys come home.
    // Pattern shows all eight channels as lines; Song the chain.
    ip->setButtonObj(interfaceButtonSong, buttonPress, functChainView);
    ip->setButtonObj(interfaceButtonPattern, buttonPress, functLinesView);

    // Params is home: the channel's settings (and back from a scene).
    ip->setButtonObj(interfaceButtonParams, buttonPress, functParamsView);
    ip->setButtonObj(interfaceButtonNote, buttonPress, functNotesView);
    // File opens Sets here, Shift + File the stock project screen.
    ip->setButtonObj(interfaceButtonFile, buttonPress, functSetsView);
    // Master opens the global key and tempo, Shift + Master the stock mixer.
    ip->setButtonObj(interfaceButtonMaster, buttonPress, functGlobalView);
    // Fx 1 opens Mod (the firmware calls this button Fx2); pressing it again
    // moves on through Mod A-D.
    ip->setButtonObj(interfaceButtonFx2, buttonPress, functModView);
    // Fx 2 opens Shift (the firmware calls this button Fx1); pressing it
    // again switches between the steps and their settings.
    ip->setButtonObj(interfaceButtonFx1, buttonPress, functShiftView);
    // Instr opens the Synth page: the channel's hardware synth. The Original
    // has no Instr key; Params (with or without Shift) does the same.
    ip->setButtonObj(interfaceButtonInstr, buttonPress, functSynthView);

    // The first time this screen opens the SD card is ready, so this is
    // where the working state from before the last power-off comes back.
    euclidSets::bootLoad();
    nameSamplerChannels(-1, false); // a sample may have been loaded meanwhile
    {
        static bool synthsLoaded = false;
        if (!synthsLoaded)
        {
            euclidSynth::loadAssignments();
            synthsLoaded = true;
        }
    }
    lastChangeCount = euclidEngine.getChangeCount();

    lastPlayhead = -2;
    lastNextNote = -2;
    showDefaultScreen();
    setDefaultScreenFunct();
}

void cEuclidParams::stop()
{
    moduleRefresh = 0;
    heldEntryPads = 0;

    // Release any auditioned notes still held.
    for (uint8_t i = 0; i < 48; i++)
    {
        if (heldPadNote[i] >= 0)
        {
            sendMidiNoteOff(static_cast<uint8_t>(heldPadNote[i]), 0, heldPadChannel[i], false);
            heldPadNote[i] = -1;
        }
    }

    mtPadBoard.releaseAllInstruments();
    stopPadBlinks();
    padsBacklight.clearGlobalPads(1, 1, 0);
}

void cEuclidParams::setDefaultScreenFunct()
{
    ip->clearButtonsRange(interfaceButton0, interfaceButton7);
    ip->clearAllPots();

    ip->setButtonObj(interfaceButtonPlay, buttonPress, functPlayAction);
    ip->setButtonObj(interfaceButtonRec, functRec);

    ip->setButtonObj(interfaceButtonUp, buttonPress, functUp);
    ip->setButtonObj(interfaceButtonDown, buttonPress, functDown);
    ip->setButtonObj(interfaceButtonLeft, buttonPress, functLeft);
    ip->setButtonObj(interfaceButtonRight, buttonPress, functRight);
    ip->setButtonObj(interfaceButtonDelete, buttonPress, functDelete);
    ip->setButtonObj(interfaceButtonInsert, buttonPress, functInsert);

    for (uint8_t b = interfaceButton0; b <= interfaceButton7; b++)
    {
        ip->setButtonObj(b, functChannelButton);
    }

    // Shift means "apply now" here (and octaves on the Note page), so the
    // stock Shift acceleration is off.
    ip->setPotObj(interfacePot0, functEncoder, false);
    ip->setPadsGlobal(functPads);
}

void cEuclidParams::selectChannel(uint8_t ch)
{
    if (ch >= euclid::kChannels)
        return;
    selectedChannel = ch;
    noteCursor = 0;
    replaceNextNote = false;
    if (view == View::synth && synthPatches)
        openPatches(true); // that channel's synth's patches
    if (view == View::lines)
        refreshLines(); // the frame moves to it
    refreshTitle();
    if (view == View::params)
        refreshParams();
    else if (view == View::mod)
        refreshMod();
    else if (view == View::shift)
        refreshShift();
    else if (view == View::synth)
        refreshSynth();
    else
        refreshNotes();
    refreshFrame();
    refreshChannels();
    refreshRing();
    refreshPads();
    display.synchronizeRefresh();
}

void cEuclidParams::moveFocus(int8_t delta)
{
    int next = static_cast<int>(selectedParam) + delta;
    if (next < 0)
        next = 0;
    if (next >= paramCount)
        next = paramCount - 1;
    selectedParam = static_cast<uint8_t>(next);
    refreshFrame();
    refreshPads();
}

void cEuclidParams::moveNoteCursor(int8_t delta)
{
    euclid::Rhythm r = euclidEngine.getRhythm(selectedChannel);
    const int entries = euclid::entryCount(r);
    int next = static_cast<int>(noteCursor) + delta;
    if (next < 0)
        next = 0;
    if (next >= entries)
        next = entries - 1;
    noteCursor = static_cast<uint8_t>(next);
    refreshFrame();
}

void cEuclidParams::toggleMute(uint8_t ch)
{
    euclid::Expression x = euclidEngine.getExpression(ch);
    euclidEngine.setMuted(ch, !x.muted);
    refreshChannels();
    if (ch == selectedChannel)
        refreshRing();
}

static int clampInt(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

void cEuclidParams::changeParam(int16_t delta)
{
    const uint8_t ch = selectedChannel;
    euclid::Rhythm r = euclidEngine.getRhythm(ch);
    euclid::Expression x = euclidEngine.getExpression(ch);
    bool rhythmChanged = false;
    const int step = delta > 0 ? 1 : -1;

    switch (selectedParam)
    {
        case paramSteps:
            euclidEngine.setSteps(ch, static_cast<uint8_t>(clampInt(r.steps + delta, 1, euclid::kMaxSteps)));
            rhythmChanged = true;
            break;
        case paramPulses:
            euclidEngine.setPulses(ch, static_cast<uint8_t>(clampInt(r.pulses + delta, 0, r.steps)));
            rhythmChanged = true;
            break;
        case paramRotation:
        {
            int rot = (static_cast<int>(r.rotation) + delta) % r.steps;
            if (rot < 0)
                rot += r.steps;
            euclidEngine.setRotation(ch, static_cast<uint8_t>(rot));
            rhythmChanged = true;
            break;
        }
        case paramDivision:
        {
            int d = clampInt(static_cast<int>(r.division) + step, 0,
                             static_cast<int>(euclid::Division::count) - 1);
            euclidEngine.setDivision(ch, static_cast<euclid::Division>(d));
            rhythmChanged = true;
            break;
        }
        case paramMode:
        {
            int m = clampInt(static_cast<int>(r.mode) + step, 0, static_cast<int>(euclid::NoteMode::count) - 1);
            euclidEngine.setMode(ch, static_cast<euclid::NoteMode>(m));
            break;
        }
        case paramChord:
            setRowValue(paramChord, rowValue(paramChord, false) + step);
            return;
        case paramChordChance:
            setRowValue(paramChordChance, rowValue(paramChordChance, false) + delta);
            return;
        case paramName:
            setRowValue(paramName, clampInt(rowValue(paramName, false) + step, 0, euclid::channelNameCount() - 1));
            return;
        case paramKey:
            // One row cycles Follow, +1 ... +11, Own (SEQ's or the generator's).
            setRowValue(paramKey, clampInt(rowValue(paramKey, false) + step, 0, 12));
            return;
        case paramTranspose:
            setRowValue(paramTranspose, rowValue(paramTranspose, false) + delta);
            return;
        case paramOwnRoot:
            x.ownRoot = static_cast<uint8_t>((x.ownRoot + 12 + step) % 12);
            euclidEngine.setExpression(ch, x);
            break;
        case paramOwnScale:
            x.ownScale = static_cast<uint8_t>(clampInt(x.ownScale + step, 0, euclid::scaleCount() - 1));
            euclidEngine.setExpression(ch, x);
            break;
        case paramOctave:
            x.octave = static_cast<uint8_t>(clampInt(x.octave + step, 0, 8));
            euclidEngine.setExpression(ch, x);
            break;
        case paramRange:
            x.range = static_cast<uint8_t>(clampInt(x.range + step, 1, 4));
            euclidEngine.setExpression(ch, x);
            break;
        case paramNoteOrder:
        {
            int d = clampInt(static_cast<int>(r.direction) + step, 0,
                             static_cast<int>(euclid::NoteDirection::count) - 1);
            euclidEngine.setDirection(ch, static_cast<euclid::NoteDirection>(d));
            break;
        }
        case paramVelocity:
            euclidEngine.setVelocity(ch, static_cast<uint8_t>(clampInt(x.velocity + delta, 1, 127)));
            break;
        case paramGate:
            euclidEngine.setGate(ch, static_cast<uint8_t>(clampInt(x.gate + delta, 1, 100)));
            break;
        case paramChance:
            euclidEngine.setProbability(ch, static_cast<uint8_t>(clampInt(x.probability + delta, 0, 100)));
            break;
        case paramMidiChannel:
            euclidEngine.setMidiChannel(ch, static_cast<uint8_t>(clampInt(x.midiChannel + delta, 1, 16)));
            break;

        case paramEnabled: euclidEngine.setEnabled(ch, delta > 0); break;
        default: return;
    }

    if (rhythmChanged)
    {
        // Steps can pull pulses and rotation down with it.
        refreshParam(paramSteps);
        refreshParam(paramPulses);
        refreshParam(paramRotation);
        refreshParam(paramDivision);
    }
    else
    {
        refreshParam(selectedParam);
    }
    if (selectedParam == paramMode || selectedParam == paramKey)
        refreshParams(); // which rows apply has changed
    refreshTitle();
    refreshChannels();
    refreshRing();
    refreshPads();
}

//==============================================================================================
// Note list editing

namespace
{
// The SEQ list as (note, tied) pairs, for editing whole entries. A tied
// note plays together with the one before it.
struct NoteList
{
    uint8_t note[euclid::kMaxNotes];
    bool tied[euclid::kMaxNotes];
    uint8_t count = 0;

    explicit NoteList(const euclid::Rhythm& r)
    {
        count = r.noteCount;
        for (uint8_t i = 0; i < count; i++)
        {
            note[i] = r.notes[i];
            tied[i] = i > 0 && (r.tieMask & (1u << i));
        }
    }

    uint16_t tieMask() const
    {
        uint16_t m = 0;
        for (uint8_t i = 1; i < count; i++)
            if (tied[i])
                m |= 1u << i;
        return m;
    }

    void erase(uint8_t at, uint8_t n)
    {
        for (uint8_t i = at; i + n < count; i++)
        {
            note[i] = note[i + n];
            tied[i] = tied[i + n];
        }
        count -= n;
        if (count > 0)
            tied[0] = false;
    }

    bool insert(uint8_t at, uint8_t value, bool tie)
    {
        if (count >= euclid::kMaxNotes)
            return false;
        for (uint8_t i = count; i > at; i--)
        {
            note[i] = note[i - 1];
            tied[i] = tied[i - 1];
        }
        note[at] = value;
        tied[at] = tie;
        count++;
        return true;
    }

    void store(uint8_t ch) const { euclidEngine.setNotes(ch, note, count, tieMask()); }
};
} // namespace

void cEuclidParams::addNote(uint8_t note)
{
    const uint8_t ch = selectedChannel;
    euclid::Rhythm r = euclidEngine.getRhythm(ch);
    NoteList list(r);

    // After a clear the list holds one leftover note (a channel can't be
    // empty); the first pad replaces it instead of adding after it.
    if (replaceNextNote)
    {
        replaceNextNote = false;
        list.count = 1;
        list.note[0] = note;
        list.tied[0] = false;
        noteCursor = 0;
    }
    else if (heldEntryPads > 0)
    {
        // Another pad is still down: add to the entry it started.
        const uint8_t last = static_cast<uint8_t>(euclid::entryCount(r) - 1);
        const uint8_t start = euclid::entryStart(r, last);
        const uint8_t len = euclid::entryLength(r, last);
        for (uint8_t i = 0; i < len; i++)
            if (list.note[start + i] == note)
                return; // already in the chord
        if (len >= euclid::kMaxEntryNotes || !list.insert(list.count, note, true))
            return;
        noteCursor = last;
    }
    else
    {
        if (!list.insert(list.count, note, false))
            return;
        noteCursor = static_cast<uint8_t>(euclid::entryCount(r));
    }

    list.store(ch);
    refreshNotes();
    refreshFrame();
}

void cEuclidParams::transposeNote(int16_t delta)
{
    const uint8_t ch = selectedChannel;
    euclid::Rhythm r = euclidEngine.getRhythm(ch);
    if (noteCursor >= euclid::entryCount(r))
        return;
    replaceNextNote = false;

    NoteList list(r);
    const uint8_t start = euclid::entryStart(r, noteCursor);
    const uint8_t len = euclid::entryLength(r, noteCursor);
    for (uint8_t i = 0; i < len; i++)
        list.note[start + i] = static_cast<uint8_t>(clampInt(list.note[start + i] + delta, 0, 127));
    list.store(ch);
    refreshNotes();
}

void cEuclidParams::deleteNote()
{
    const uint8_t ch = selectedChannel;
    euclid::Rhythm r = euclidEngine.getRhythm(ch);
    const uint8_t entries = euclid::entryCount(r);
    if (entries <= 1 || noteCursor >= entries)
        return; // a channel always keeps at least one entry; Shift+Delete clears

    NoteList list(r);
    list.erase(euclid::entryStart(r, noteCursor), euclid::entryLength(r, noteCursor));
    list.store(ch);
    if (noteCursor >= entries - 1)
        noteCursor = static_cast<uint8_t>(entries - 2);
    refreshNotes();
    refreshFrame();
}

void cEuclidParams::clearNotes()
{
    const uint8_t ch = selectedChannel;
    euclid::Rhythm r = euclidEngine.getRhythm(ch);
    euclidEngine.setNotes(ch, r.notes, 1, 0);
    noteCursor = 0;
    replaceNextNote = true;
    refreshNotes();
    refreshFrame();
}

//==============================================================================================
// Global view

void cEuclidParams::changeGlobal(int16_t delta)
{
    const int step = delta > 0 ? 1 : -1;
    euclid::Key k = euclidEngine.getGlobalKey();

    switch (selectedGlobal)
    {
        case globalTempo:
        {
            // Same rules as the stock Song screen: whole BPM steps, and
            // left alone while following an external clock.
            if (!sequencer.isSetInternalClock())
                return;
            float t = static_cast<float>(static_cast<int>(mtProject.values.globalTempo)) + delta;
            if (t < Sequencer::MIN_TEMPO)
                t = Sequencer::MIN_TEMPO;
            if (t > Sequencer::MAX_TEMPO)
                t = Sequencer::MAX_TEMPO;
            mtProject.values.globalTempo = t;
            newFileManager.setProjectStructChanged();
            break;
        }
        case globalRoot:
            k.root = static_cast<uint8_t>((k.root + 12 + step) % 12);
            euclidEngine.setGlobalKey(k);
            break;
        case globalScale:
            k.scale = static_cast<uint8_t>(clampInt(k.scale + step, 0, euclid::scaleCount() - 1));
            euclidEngine.setGlobalKey(k);
            break;
        case globalLaunch:
            euclidEngine.setLaunch(static_cast<euclid::Launch>(clampInt(
              static_cast<int>(euclidEngine.getLaunch()) + step, 0, static_cast<int>(euclid::Launch::count) - 1)));
            break;
        default: return;
    }
    refreshGlobal();
    refreshTitle();
    refreshPads();
}

//==============================================================================================
// Messages

namespace
{
uint32_t nowMs()
{
    return TrackerImpl::getInstance()->getPlatform()->getTimer()->millis();
}
} // namespace

// The notes every heard channel plays go into a count of pitch classes that
// fades (to half in about 1.7 s); every 150 ms the best-fitting triad is
// worked out, and the Pattern page's title follows it.
void cEuclidParams::updateHarmony()
{
    const uint32_t now = nowMs();
    for (uint8_t ch = 0; ch < euclid::kChannels; ch++)
    {
        const uint32_t count = euclidEngine.getNoteCount(ch);
        if (count == harmonyCount[ch])
            continue;
        harmonyCount[ch] = count;
        const euclid::Expression x = euclidEngine.getExpression(ch);
        if (!x.enabled || x.muted)
            continue;
        uint8_t notes[8];
        const uint8_t n = euclidEngine.getLastNotes(ch, notes);
        for (uint8_t i = 0; i < n; i++)
            harmonyWeight[notes[i] % 12] += 1.0f;
    }
    if (now - harmonyMs < 150)
        return;
    const float fade = static_cast<float>(now - harmonyMs) / 1500.0f; // 0.1 a step: 6% lost each 150 ms
    harmonyMs = now;
    for (float& w : harmonyWeight)
        w = w * (fade < 1.0f ? 1.0f - fade * 0.6f : 0.4f);
    const euclid::HarmonyGuess g = euclidEngine.isRunning() ? euclid::guessHarmony(harmonyWeight) : euclid::HarmonyGuess();
    const int8_t root = g.known ? static_cast<int8_t>(g.root) : -1;
    if (root != harmonyRoot || (g.known && g.minor != harmonyMinor))
    {
        harmonyRoot = root;
        harmonyMinor = g.minor;
        if (view == View::lines)
            refreshTitle();
    }
}

void cEuclidParams::showMessage(const char* text)
{
    snprintf(message, sizeof(message), "%s", text);
    // A long one: the start for a moment, then a letter at a time to the
    // end, which stays a little before the title comes back.
    const int extra = static_cast<int>(strlen(message)) - kTitleChars;
    messageScroll = 0;
    messageScrollNext = nowMs() + 1000;
    messageUntil = nowMs() + (extra > 0 ? 1000 + extra * 180 + 1500 : 5000);
    refreshTitle();
}

void cEuclidParams::clearChannel(uint8_t ch)
{
    // What the channel is called and where it plays stay; the music goes.
    const euclid::Expression old = euclidEngine.getExpression(ch);
    euclid::Expression x;
    x.enabled = old.enabled;
    x.muted = old.muted;
    x.midiChannel = old.midiChannel;
    x.output = old.output;
    x.instrument = old.instrument;
    euclid::Rhythm r;
    const euclid::Key k = euclidEngine.getGlobalKey();
    r.homeRoot = k.root;
    r.homeScale = k.scale;
    euclidEngine.setRhythm(ch, r);
    euclidEngine.setExpression(ch, x);
}

void cEuclidParams::requestClear(bool all)
{
    const int8_t target = all ? 8 : static_cast<int8_t>(selectedChannel);
    char text[48];
    if (armedClear != target || static_cast<int32_t>(nowMs() - armedClearUntil) > 0)
    {
        armedClear = target;
        armedClearUntil = nowMs() + 3000;
        if (all)
            snprintf(text, sizeof(text), "Shift + Delete again: clear all channels");
        else
            snprintf(text, sizeof(text), "Shift + Delete again: clear channel %d", selectedChannel + 1);
        showMessage(text);
        return;
    }
    armedClear = -1;
    for (uint8_t ch = 0; ch < euclid::kChannels; ch++)
        if (all || ch == selectedChannel)
            clearChannel(ch);
    const bool staged = euclidEngine.isRunning();
    if (all)
        snprintf(text, sizeof(text), "All channels cleared%s", staged ? ": Rec to apply" : "");
    else
        snprintf(text, sizeof(text), "Channel %d cleared%s", selectedChannel + 1, staged ? ": Rec to apply" : "");
    showMessage(text);
    refreshAfterCommit();
}

void cEuclidParams::updateMessage()
{
    const int extra = static_cast<int>(strlen(message)) - kTitleChars;
    if (messageUntil != 0 && extra > 0 && messageScroll < extra &&
        static_cast<int32_t>(nowMs() - messageScrollNext) >= 0)
    {
        messageScroll++;
        messageScrollNext = nowMs() + 180;
        refreshTitle();
    }
    // Put the normal title back when a message runs out.
    if (messageUntil != 0 && static_cast<int32_t>(messageUntil - nowMs()) <= 0)
    {
        messageUntil = 0;
        refreshTitle();
    }
}

//==============================================================================================
// Committing

void cEuclidParams::commitAll()
{
    euclidEngine.commitAll();
    refreshAfterCommit();
}

void cEuclidParams::commitChannel(uint8_t ch)
{
    euclidEngine.commit(ch);
    refreshAfterCommit();
}

void cEuclidParams::refreshAfterCommit()
{
    refreshTitle();
    if (view == View::params)
        refreshParams();
    else if (view == View::notes)
        refreshNotes();
    else if (view == View::global)
        refreshGlobal();
    else if (view == View::mod)
        refreshMod();
    else if (view == View::shift)
        refreshShift();
    refreshTitle();
    refreshChannels();
    refreshRing();
    refreshPads();
}

//==============================================================================================
// Sets view

void cEuclidParams::moveSetCursor(int8_t delta)
{
    int next = static_cast<int>(setCursor) + delta;
    if (next < 0)
        next = 0;
    if (next >= euclid::kSetSlots)
        next = euclid::kSetSlots - 1;
    setCursor = static_cast<uint8_t>(next);
    if (armedAction != SetAction::none)
    {
        armedAction = SetAction::none;
        refreshChannels();
    }
    setStatus[0] = 0;
    refreshSets();
    refreshPads();
}

void cEuclidParams::setSoftKey(uint8_t key)
{
    const uint8_t slot = setCursor + 1;

    if (key == 7)
    {
        setView(viewBeforeSets);
        setDefaultScreenFunct();
        return;
    }

    if (key == 0)
    {
        // Saving over a set that's already there needs a second press.
        if (euclidSets::exists(slot) && armedAction != SetAction::save)
        {
            armedAction = SetAction::save;
            snprintf(setStatus, sizeof(setStatus), "Save over Set %d? Press again", slot);
        }
        else
        {
            armedAction = SetAction::none;
            if (euclidSets::save(slot))
                snprintf(setStatus, sizeof(setStatus), "Saved to Set %d", slot);
            else
                snprintf(setStatus, sizeof(setStatus), "Save failed: check SD card");
        }
    }
    else if (key == 1)
    {
        if (!euclidSets::exists(slot))
        {
            armedAction = SetAction::none;
            snprintf(setStatus, sizeof(setStatus), "Set %d is empty", slot);
        }
        else if (armedAction != SetAction::load)
        {
            // Loading replaces the working copy, so it needs a second press.
            armedAction = SetAction::load;
            snprintf(setStatus, sizeof(setStatus), "Load Set %d? Press again", slot);
        }
        else
        {
            armedAction = SetAction::none;
            if (euclidSets::load(slot))
                snprintf(setStatus, sizeof(setStatus), "Loaded Set %d%s", slot,
                         euclidEngine.isRunning() ? ", from the next bar" : "");
            else
                snprintf(setStatus, sizeof(setStatus), "Couldn't load Set %d", slot);
        }
    }
    else
    {
        armedAction = SetAction::none;
        setStatus[0] = 0;
    }

    refreshSets();
    refreshChannels();
    refreshRing();
}

void cEuclidParams::duplicateNote()
{
    const uint8_t ch = selectedChannel;
    euclid::Rhythm r = euclidEngine.getRhythm(ch);
    if (noteCursor >= euclid::entryCount(r))
        return;
    const uint8_t start = euclid::entryStart(r, noteCursor);
    const uint8_t len = euclid::entryLength(r, noteCursor);
    if (r.noteCount + len > kNoteSlots)
        return;
    replaceNextNote = false;

    NoteList list(r);
    for (uint8_t i = 0; i < len; i++)
        list.insert(static_cast<uint8_t>(start + len + i), r.notes[start + i], i > 0);
    list.store(ch);
    noteCursor++;
    refreshNotes();
    refreshFrame();
}

//==============================================================================================
static void functEncoder(int16_t value)
{
    if (EP->isNoteSettings())
    {
        ShiftEdits shift;
        EP->changeParam(value);
        return;
    }
    // On the lines Shift changes steps rather than skipping Rec.
    if (EP->view == cEuclidParams::View::lines)
    {
        EP->changeLines(value, EP->buttons->isButtonPressed(interfaceButtonShift));
        return;
    }
    ShiftEdits shift;
    if (EP->view == cEuclidParams::View::chain)
    {
        EP->changeChain(value);
        return;
    }
    if (EP->view == cEuclidParams::View::synth)
    {
        EP->changeSynth(value);
        return;
    }
    if (EP->view == cEuclidParams::View::shift)
    {
        EP->changeShift(value);
        return;
    }
    if (EP->view == cEuclidParams::View::mod)
    {
        EP->changeMod(value);
        return;
    }
    if (EP->view == cEuclidParams::View::global)
    {
        EP->changeGlobal(value);
        return;
    }
    if (EP->view == cEuclidParams::View::sets)
    {
        EP->moveSetCursor(value > 0 ? 1 : -1);
        return;
    }
    if (EP->view == cEuclidParams::View::notes)
    {
        if (euclidEngine.getRhythm(EP->selectedChannel).mode != euclid::NoteMode::seq)
            return; // the notes shown come from the mode, not a list
        int16_t step = value > 0 ? 1 : -1;
        if (EP->buttons->isButtonPressed(interfaceButtonShift))
            EP->transposeNote(step * 12);
        else
            EP->transposeNote(value);
        return;
    }
    EP->changeParam(value);
}

static void functUp()
{
    if (EP->isNoteSettings())
    {
        const uint8_t row = EP->noteRowOf(EP->selectedParam);
        EP->selectedParam = cEuclidParams::kNoteParams[row > 0 ? row - 1 : 0];
        EP->refreshFrame();
        EP->refreshPads();
        return;
    }
    if (EP->view == cEuclidParams::View::lines)
    {
        EP->selectChannel((EP->selectedChannel + euclid::kChannels - 1) % euclid::kChannels);
        return;
    }
    if (EP->view == cEuclidParams::View::chain)
    {
        EP->moveChainRow(-1);
        return;
    }
    if (EP->view == cEuclidParams::View::synth && EP->synthPatches)
    {
        EP->movePatchCursor(-1);
        return;
    }
    if (EP->view == cEuclidParams::View::synth)
    {
        if (EP->selectedSynthRow > 0)
            EP->selectedSynthRow--;
        EP->refreshFrame();
        EP->refreshPads();
        return;
    }
    if (EP->view == cEuclidParams::View::shift)
    {
        if (EP->shiftSettings)
        {
            if (EP->selectedShiftRow > 0)
                EP->selectedShiftRow--;
            EP->refreshFrame();
            EP->refreshPads();
        }
        else
        {
            EP->moveShiftCursor(-8);
        }
        return;
    }
    if (EP->view == cEuclidParams::View::mod)
    {
        if (EP->selectedModRow > 0)
            EP->selectedModRow--;
        EP->refreshFrame();
        EP->refreshPads();
        return;
    }
    if (EP->view == cEuclidParams::View::global)
    {
        if (EP->selectedGlobal > 0)
            EP->selectedGlobal--;
        EP->refreshFrame();
        EP->refreshPads();
        return;
    }
    if (EP->view == cEuclidParams::View::sets)
    {
        EP->moveSetCursor(-4);
        return;
    }
    if (EP->view == cEuclidParams::View::notes)
        EP->moveNoteCursor(-static_cast<int8_t>(cEuclidParams::kNoteColumns));
    else
        EP->moveFocus(-1);
}

static void functDown()
{
    if (EP->isNoteSettings())
    {
        const uint8_t row = EP->noteRowOf(EP->selectedParam);
        EP->selectedParam =
          cEuclidParams::kNoteParams[row + 1 < cEuclidParams::kNoteParamCount ? row + 1 : row];
        EP->refreshFrame();
        EP->refreshPads();
        return;
    }
    if (EP->view == cEuclidParams::View::lines)
    {
        EP->selectChannel((EP->selectedChannel + 1) % euclid::kChannels);
        return;
    }
    if (EP->view == cEuclidParams::View::chain)
    {
        EP->moveChainRow(1);
        return;
    }
    if (EP->view == cEuclidParams::View::synth && EP->synthPatches)
    {
        EP->movePatchCursor(1);
        return;
    }
    if (EP->view == cEuclidParams::View::synth)
    {
        if (EP->selectedSynthRow + 1 < EP->synthRowCount())
            EP->selectedSynthRow++;
        EP->refreshFrame();
        EP->refreshPads();
        return;
    }
    if (EP->view == cEuclidParams::View::shift)
    {
        if (EP->shiftSettings)
        {
            if (EP->selectedShiftRow + 1 < EP->shiftRows())
                EP->selectedShiftRow++;
            EP->refreshFrame();
            EP->refreshPads();
        }
        else
        {
            EP->moveShiftCursor(8);
        }
        return;
    }
    if (EP->view == cEuclidParams::View::mod)
    {
        if (EP->selectedModRow + 1 < EP->modVisibleCount)
            EP->selectedModRow++;
        EP->refreshFrame();
        EP->refreshPads();
        return;
    }
    if (EP->view == cEuclidParams::View::global)
    {
        if (EP->selectedGlobal + 1 < cEuclidParams::globalCount)
            EP->selectedGlobal++;
        EP->refreshFrame();
        EP->refreshPads();
        return;
    }
    if (EP->view == cEuclidParams::View::sets)
    {
        EP->moveSetCursor(4);
        return;
    }
    if (EP->view == cEuclidParams::View::notes)
        EP->moveNoteCursor(cEuclidParams::kNoteColumns);
    else
        EP->moveFocus(1);
}

static void functLeft()
{
    if (EP->view == cEuclidParams::View::lines)
    {
        if (EP->buttons->isButtonPressed(interfaceButtonShift))
            EP->orderLines(-1); // Shift: the note order
        else
            EP->rotateLines(-1);
        return;
    }
    if (EP->view == cEuclidParams::View::chain)
    {
        EP->moveChainField(-1);
        return;
    }
    if (EP->view == cEuclidParams::View::shift && !EP->shiftSettings)
    {
        EP->moveShiftCursor(-1);
        return;
    }
    if (EP->view == cEuclidParams::View::sets)
    {
        EP->moveSetCursor(-1);
        return;
    }
    if (EP->view == cEuclidParams::View::notes)
    {
        EP->moveNoteCursor(-1);
        return;
    }
    EP->selectChannel(EP->selectedChannel == 0 ? euclid::kChannels - 1 : EP->selectedChannel - 1);
}

static void functRight()
{
    if (EP->view == cEuclidParams::View::lines)
    {
        if (EP->buttons->isButtonPressed(interfaceButtonShift))
            EP->orderLines(1);
        else
            EP->rotateLines(1);
        return;
    }
    if (EP->view == cEuclidParams::View::chain)
    {
        EP->moveChainField(1);
        return;
    }
    if (EP->view == cEuclidParams::View::shift && !EP->shiftSettings)
    {
        EP->moveShiftCursor(1);
        return;
    }
    if (EP->view == cEuclidParams::View::sets)
    {
        EP->moveSetCursor(1);
        return;
    }
    if (EP->view == cEuclidParams::View::notes)
    {
        EP->moveNoteCursor(1);
        return;
    }
    EP->selectChannel((EP->selectedChannel + 1) % euclid::kChannels);
}

static void functDelete()
{
    // Shift + Delete on Params or Pattern: back to defaults (asks first).
    if ((EP->view == cEuclidParams::View::params || EP->view == cEuclidParams::View::lines) &&
        EP->buttons->isButtonPressed(interfaceButtonShift))
    {
        EP->requestClear(EP->view == cEuclidParams::View::lines);
        return;
    }
    if (EP->view == cEuclidParams::View::chain)
    {
        EP->deleteChainStep();
        return;
    }
    // Shift + Delete removes the selected patch from the card.
    if (EP->view == cEuclidParams::View::synth && EP->synthPatches)
    {
        if (EP->buttons->isButtonPressed(interfaceButtonShift))
            EP->deletePatch();
        return;
    }
    if (EP->view == cEuclidParams::View::shift && !EP->shiftSettings)
    {
        ShiftEdits shift;
        EP->clearShiftStep(EP->buttons->isButtonPressed(interfaceButtonShift));
        return;
    }
    if (EP->view != cEuclidParams::View::notes || EP->notesSettings ||
        euclidEngine.getRhythm(EP->selectedChannel).mode != euclid::NoteMode::seq)
        return;
    ShiftEdits shift;
    if (EP->buttons->isButtonPressed(interfaceButtonShift))
        EP->clearNotes();
    else
        EP->deleteNote();
}

static void functInsert()
{
    // On the Synth page: send the whole patch, so the synth matches the
    // screen. Shift + Insert does the opposite: it gets the synth's patch.
    // In the patch list, Insert loads the selected patch.
    if (EP->view == cEuclidParams::View::chain)
    {
        EP->insertChainStep();
        return;
    }
    if (EP->view == cEuclidParams::View::synth && EP->synthPatches &&
        !EP->buttons->isButtonPressed(interfaceButtonShift))
    {
        EP->loadPatch();
        return;
    }
    if (EP->view == cEuclidParams::View::synth)
    {
        if (!EP->buttons->isButtonPressed(interfaceButtonShift))
            euclidSynth::sendAll(EP->selectedChannel);
        else if (!euclidSynth::requestPatch(EP->selectedChannel))
            EP->showMessage(euclidSynth::error());
        EP->refreshTitle();
        return;
    }
    ShiftEdits shift;
    if (EP->view == cEuclidParams::View::notes && !EP->notesSettings &&
        euclidEngine.getRhythm(EP->selectedChannel).mode == euclid::NoteMode::seq)
        EP->duplicateNote();
}

static void functParamsView()
{
    // Rec + Params: the scene playing (with its edits) becomes your settings.
    if (EP->recHeld)
    {
        EP->recCommittedChannel = true; // Rec was for this, not a commit
        char text[48];
        const int8_t scene = euclidScenes::active();
        if (euclidScenes::keepAsHome())
            snprintf(text, sizeof(text), "Scene %c is now your settings", 'A' + scene);
        else
            snprintf(text, sizeof(text), "No scene playing to keep");
        EP->showMessage(text);
        EP->refreshTitle();
        return;
    }
    // Shift + Params goes back from a scene to what was playing before it.
    // Params alone just opens the page (the scene's, while one plays).
    if (EP->buttons->isButtonPressed(interfaceButtonShift) && euclidScenes::returnHome())
    {
        char text[48];
        if (euclidEngine.isRunning())
        {
            euclid::Launch l = euclidEngine.getLaunch();
            snprintf(text, sizeof(text), "Back to yours from the next %s",
                     l == euclid::Launch::beat ? "beat" : (l == euclid::Launch::bar ? "bar" : "phrase"));
        }
        else
            snprintf(text, sizeof(text), "Back to your settings");
        EP->showMessage(text);
        if (EP->view != cEuclidParams::View::params)
        {
            EP->setView(cEuclidParams::View::params);
            EP->setDefaultScreenFunct();
        }
        EP->refreshAfterCommit();
        return;
    }
    if (EP->view == cEuclidParams::View::params)
        return;
    EP->setView(cEuclidParams::View::params);
    EP->setDefaultScreenFunct();
}

static void functSynthView()
{
    // Shift + Instr on a sampler channel: the stock instrument editor, for
    // what the Synth page doesn't have (envelopes, LFOs, the rest).
    const euclid::Expression x = euclidEngine.getExpression(EP->selectedChannel);
    if (x.output == euclid::ChannelOutput::sampler && EP->buttons->isButtonPressed(interfaceButtonShift))
    {
        mtProject.values.lastUsedInstrument = x.instrument;
        functSwitchModule(interfaceButtonInstr);
        return;
    }
    if (EP->view == cEuclidParams::View::synth)
        return;
    euclidSynth::scan(); // pick up definition files added to the card
    EP->setView(cEuclidParams::View::synth);
    EP->setDefaultScreenFunct();
}

// Like Fx 1 on the Mod page, pressing Fx 2 again moves on to the next lane
// (Pitch, Mode, Division, Order, Chord); Shift + Fx 2 switches between the
// lane's steps and its settings.
static void functLinesView()
{
    if (EP->view == cEuclidParams::View::lines)
        return;
    EP->setView(cEuclidParams::View::lines);
    EP->setDefaultScreenFunct();
}

static void functChainView()
{
    if (EP->view == cEuclidParams::View::chain)
        return;
    EP->setView(cEuclidParams::View::chain);
    EP->setDefaultScreenFunct();
}

static void functShiftView()
{
    if (EP->view == cEuclidParams::View::shift)
    {
        if (EP->buttons->isButtonPressed(interfaceButtonShift))
        {
            EP->shiftSettings = !EP->shiftSettings;
            EP->setView(cEuclidParams::View::shift);
        }
        else
        {
            EP->setShiftRowValue(cEuclidParams::shiftRowLane, (EP->shiftLane + 1) % (1 + euclid::kLaneKinds));
        }
        return;
    }
    EP->shiftSettings = false;
    EP->setView(cEuclidParams::View::shift);
    EP->setDefaultScreenFunct();
}

static void functModView()
{
    // Pressing Fx 1 again moves on to the next modulator: A, B, C, D, A...
    if (EP->view == cEuclidParams::View::mod)
    {
        EP->selectedMod = static_cast<uint8_t>((EP->selectedMod + 1) % euclid::kModsPerChannel);
        EP->selectedModRow = 0;
        EP->setView(cEuclidParams::View::mod);
        return;
    }
    EP->setView(cEuclidParams::View::mod);
    EP->setDefaultScreenFunct();
}

static void functGlobalView()
{
    // Shift + Master: the stock mixer and master effects.
    if (EP->buttons->isButtonPressed(interfaceButtonShift))
    {
        functSwitchModule(interfaceButtonMaster);
        return;
    }
    if (EP->view == cEuclidParams::View::global)
        return;
    EP->setView(cEuclidParams::View::global);
    EP->setDefaultScreenFunct();
}

static void functSetsView()
{
    // On the Synth page File is the channel's patches: File opens and closes
    // the list, Shift + File saves the patch.
    if (EP->view == cEuclidParams::View::synth)
    {
        if (EP->buttons->isButtonPressed(interfaceButtonShift))
            EP->savePatch();
        else
            EP->openPatches(!EP->synthPatches);
        return;
    }
    // Elsewhere Shift + File is the stock project screen (projects hold the
    // samples and instruments).
    if (EP->buttons->isButtonPressed(interfaceButtonShift))
    {
        functSwitchModule(interfaceButtonFile);
        return;
    }
    if (EP->view == cEuclidParams::View::sets)
        return;
    EP->viewBeforeSets = EP->view;
    EP->setStatus[0] = 0;
    EP->setView(cEuclidParams::View::sets);
    EP->setDefaultScreenFunct();
}

// Note opens the note grid; pressing it again switches to the note
// settings (mode, chords, key, octave, range...) and back.
static void functNotesView()
{
    if (EP->view == cEuclidParams::View::notes)
        EP->notesSettings = !EP->notesSettings;
    else
        EP->notesSettings = false;
    if (EP->notesSettings && EP->noteRowOf(EP->selectedParam) == 0 &&
        EP->selectedParam != cEuclidParams::kNoteParams[0])
        EP->selectedParam = cEuclidParams::kNoteParams[0];
    EP->setView(cEuclidParams::View::notes);
    EP->setDefaultScreenFunct();
}

static void functPads(uint8_t pad, uint8_t state, [[maybe_unused]] int16_t velo)
{
    if (pad >= 48)
        return;

    if (state == buttonPress)
    {
        padsBacklight.setFrontLayer(1, mtConfig.values.padsLightFront, pad);

        // On a setting with fixed choices the pads pick the value.
        {
            ShiftEdits shift;
            if (EP->padSelect(pad))
                return;
        }

        uint8_t note = mtPadBoard.getNoteFromPad(pad);

        // Let the pad be heard on the channel's MIDI output.
        euclid::Expression x = euclidEngine.getExpression(EP->selectedChannel);
        if (EP->heldPadNote[pad] >= 0)
            sendMidiNoteOff(static_cast<uint8_t>(EP->heldPadNote[pad]), 0, EP->heldPadChannel[pad], false);
        sendMidiNoteOn(note, kAuditionVelocity, x.midiChannel, false);
        EP->heldPadNote[pad] = note;
        EP->heldPadChannel[pad] = x.midiChannel;

        // The Note page's pads add to the SEQ list; in other modes they only
        // play (that list isn't shown or used).
        if (EP->view == cEuclidParams::View::notes && !EP->notesSettings &&
            euclidEngine.getRhythm(EP->selectedChannel).mode == euclid::NoteMode::seq)
        {
            ShiftEdits shift;
            EP->addNote(note);
            EP->heldEntryPads++;
        }
    }
    else if (state == buttonRelease)
    {
        padsBacklight.setFrontLayer(0, 0, pad);

        if (EP->heldPadNote[pad] >= 0)
        {
            sendMidiNoteOff(static_cast<uint8_t>(EP->heldPadNote[pad]), 0, EP->heldPadChannel[pad], false);
            EP->heldPadNote[pad] = -1;
            if (EP->heldEntryPads > 0)
                EP->heldEntryPads--;
        }
    }
}

static void functChannelButton(uint8_t button, uint8_t state)
{
    if (state != buttonPress)
        return;

    uint8_t ch = button - interfaceButton0;

    // Rec held + a channel key commits just that channel.
    if (EP->recHeld)
    {
        EP->recCommittedChannel = true;
        EP->commitChannel(ch);
        return;
    }

    if (EP->view == cEuclidParams::View::sets)
    {
        EP->setSoftKey(ch);
        return;
    }

    if (EP->buttons->isButtonPressed(interfaceButtonShift))
    {
        EP->toggleMute(ch);
        return;
    }

    // From the Global page a channel key opens that channel's Params.
    if (EP->view == cEuclidParams::View::global)
    {
        EP->selectedChannel = ch;
        EP->setView(cEuclidParams::View::params);
        EP->setDefaultScreenFunct();
        EP->refreshRing();
        return;
    }

    EP->selectChannel(ch);
}

static void functPlayAction()
{
    if (sequencer.getSeqState() == Sequencer::SEQ_STATE_STOP)
    {
        sequencer.playPattern();
    }
    else if (EP->buttons->isButtonPressed(interfaceButtonShift))
    {
        // Restart from the top of the bar.
        sequencer.stop();
        sequencer.playPattern();
    }
    else
    {
        sequencer.stop();
    }
}

// Rec commits everything staged when it's released, unless a channel key
// was pressed while it was held (that committed just the channel) or a
// scene key (that saved the scene).
static void functRec([[maybe_unused]] uint8_t button, uint8_t state)
{
    if (state == buttonPress)
    {
        EP->recHeld = true;
        EP->recCommittedChannel = false;
    }
    else if (state == buttonRelease)
    {
        if (EP->recHeld && !EP->recCommittedChannel)
            EP->commitAll();
        EP->recHeld = false;
    }
}

// Scenes: tap recalls (at the next launch point while playing), Rec + tap
// saves what's on screen into the scene, Shift + tap opens the stock sample
// screen the key is named after. Shift + Params goes back to your own
// settings, Rec + Params keeps the scene as them. Params goes back to what was
// playing before the first scene. From any other page a tap also opens the
// scene's Params page, and tapping the scene that's playing only does that
// (so its edits stay).
static void functScene(uint8_t button)
{
    uint8_t scene = 0;
    if (button == interfaceButtonSampleLoad)
        scene = 0;
    else if (button == interfaceButtonSamplePlay)
        scene = 1;
    else if (button == interfaceButtonSampleEdit)
        scene = 2;
    else
        scene = 3;
    const char letter = static_cast<char>('A' + scene);
    char text[48];

    if (EP->buttons->isButtonPressed(interfaceButtonShift))
    {
        functSwitchModule(button);
        return;
    }
    if (EP->recHeld)
    {
        EP->recCommittedChannel = true; // Rec was for this, not a commit
        bool replaced = euclidScenes::has(scene);
        euclidScenes::save(scene);
        snprintf(text, sizeof(text), "%s Scene %c", replaced ? "Saved over" : "Saved", letter);
        EP->showMessage(text);
        return;
    }

    // On the lines a scene changes and the lines stay; elsewhere it opens
    // the scene's Params page.
    const bool onParams = EP->view == cEuclidParams::View::params || EP->view == cEuclidParams::View::lines;
    if (!onParams && euclidScenes::active() == scene)
    {
        EP->setView(cEuclidParams::View::params);
        EP->setDefaultScreenFunct();
        return;
    }

    if (!euclidScenes::recall(scene))
    {
        snprintf(text, sizeof(text), "Scene %c is empty: Rec + key saves it", letter);
        EP->showMessage(text);
        return;
    }
    if (!onParams)
    {
        EP->setView(cEuclidParams::View::params);
        EP->setDefaultScreenFunct();
    }

    if (euclidEngine.isRunning())
    {
        euclid::Launch l = euclidEngine.getLaunch();
        snprintf(text, sizeof(text), "Scene %c from the next %s", letter,
                 l == euclid::Launch::beat ? "beat" : (l == euclid::Launch::bar ? "bar" : "phrase"));
    }
    else
    {
        snprintf(text, sizeof(text), "Scene %c", letter);
    }
    EP->showMessage(text);
    EP->refreshAfterCommit();
}

static void functSwitchModule(uint8_t button)
{
    EP->eventFunct(eventSwitchModule, EP, &button, 0);
}
