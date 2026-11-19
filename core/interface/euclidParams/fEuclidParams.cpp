#include "euclidParams/euclidParams.h"

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
    updateFreeze();

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
        if (next != lastNextNote)
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
    // The four Sample keys are Scenes A-D here; the sample screens are still
    // reachable from the stock screens.
    ip->setButtonObj(interfaceButtonSampleLoad, buttonPress, functScene);
    ip->setButtonObj(interfaceButtonSamplePlay, buttonPress, functScene);
    ip->setButtonObj(interfaceButtonSampleEdit, buttonPress, functScene);
    ip->setButtonObj(interfaceButtonSampleRec, buttonPress, functScene);
    ip->setButtonObj(interfaceButtonSong, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonPattern, buttonPress, functSwitchModule);

    ip->setButtonObj(interfaceButtonParams, buttonPress, functParamsView);
    ip->setButtonObj(interfaceButtonNote, buttonPress, functNotesView);
    // File opens Sets here; the stock project screen is still reachable
    // from the stock screens' File key.
    ip->setButtonObj(interfaceButtonFile, buttonPress, functSetsView);
    // Master opens the global key and tempo; the stock mixer is still
    // reachable from the stock screens' Master key.
    ip->setButtonObj(interfaceButtonMaster, buttonPress, functGlobalView);
    // Fx 1 opens Mod (the firmware calls this button Fx2); pressing it again
    // moves on through Mod A-D.
    ip->setButtonObj(interfaceButtonFx2, buttonPress, functModView);

    // The first time this screen opens the SD card is ready, so this is
    // where the working state from before the last power-off comes back.
    euclidSets::bootLoad();
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
    refreshTitle();
    if (view == View::params)
        refreshParams();
    else if (view == View::mod)
        refreshMod();
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
        case globalFreeze:
            setRowValue(globalFreeze, rowValue(globalFreeze, false) + step);
            return;
        case globalLayer: euclidEngine.setLayer(step > 0); break;
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
// Freeze

namespace
{
uint32_t nowMs()
{
    return TrackerImpl::getInstance()->getPlatform()->getTimer()->millis();
}
} // namespace

void cEuclidParams::showMessage(const char* text)
{
    snprintf(message, sizeof(message), "%s", text);
    messageUntil = nowMs() + 5000;
    refreshTitle();
}

// The window ends at the nearest bar line: pressed in the first beat of a
// bar, the one just passed (you were a little late); later in the bar, the
// end of this one. With Launch set to 2 or 4 bars it snaps to those phrases.
void cEuclidParams::requestFreeze()
{
    if (freezePending)
    {
        freezePending = false;
        showMessage("Freeze cancelled");
        return;
    }
    if (!euclidEngine.isRunning())
    {
        showMessage("Press Play first: freeze takes what you play");
        return;
    }

    uint32_t phrase = euclid::kMicroStepsPerBar;
    if (euclidEngine.getLaunch() == euclid::Launch::twoBars || euclidEngine.getLaunch() == euclid::Launch::fourBars)
        phrase = euclid::launchToMicroSteps(euclidEngine.getLaunch());

    const uint32_t now = euclidEngine.getPlayTime();
    const uint32_t pos = now % phrase;
    const uint32_t beat = euclid::kMicroStepsPerBar / 4;
    freezeEnd = pos < beat ? now - pos : now - pos + phrase;

    if (freezeEnd < static_cast<uint32_t>(euclid::kMicroStepsPerBar))
    {
        // Pressed in the first beat after Play: wait for the first bar.
        freezeEnd = euclid::kMicroStepsPerBar;
    }
    freezePending = true;
    refreshTitle();
    updateFreeze();
}

void cEuclidParams::updateFreeze()
{
    if (freezePending)
    {
        if (!euclidEngine.isRunning())
        {
            freezePending = false;
            showMessage("Stopped before the bar: no freeze");
            return;
        }
        if (euclidEngine.getPlayTime() < freezeEnd || newFileManager.getStatus() != fmIdle)
            return;

        freezePending = false;
        const uint32_t bar = euclid::kMicroStepsPerBar;
        uint32_t bars = euclid::freezeLengthBars(euclidEngine.getFreezeLength());
        if (bars > freezeEnd / bar)
            bars = freezeEnd / bar; // only this much has been played

        char text[48];
        euclidFreeze::freeze(freezeEnd - bars * bar, freezeEnd, text, sizeof(text));
        showMessage(text);
        return;
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
    ShiftEdits shift;
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
    if (EP->view != cEuclidParams::View::notes)
        return;
    ShiftEdits shift;
    if (EP->buttons->isButtonPressed(interfaceButtonShift))
        EP->clearNotes();
    else
        EP->deleteNote();
}

static void functInsert()
{
    ShiftEdits shift;
    if (EP->view == cEuclidParams::View::notes)
        EP->duplicateNote();
}

static void functParamsView()
{
    if (EP->view == cEuclidParams::View::params)
        return;
    EP->setView(cEuclidParams::View::params);
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
    if (EP->view == cEuclidParams::View::global)
        return;
    EP->setView(cEuclidParams::View::global);
    EP->setDefaultScreenFunct();
}

static void functSetsView()
{
    if (EP->view == cEuclidParams::View::sets)
        return;
    EP->viewBeforeSets = EP->view;
    EP->setStatus[0] = 0;
    EP->setView(cEuclidParams::View::sets);
    EP->setDefaultScreenFunct();
}

static void functNotesView()
{
    if (EP->view == cEuclidParams::View::notes)
        return;
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

        if (EP->view == cEuclidParams::View::notes)
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
// was pressed while it was held (that committed just the channel). Shift +
// Rec freezes the last bars into a new pattern (press again to cancel).
static void functRec([[maybe_unused]] uint8_t button, uint8_t state)
{
    if (state == buttonPress)
    {
        if (EP->buttons->isButtonPressed(interfaceButtonShift))
        {
            EP->requestFreeze();
            return;
        }
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

// Scenes: tap recalls (at the next launch point while playing), Shift + tap
// saves what's on screen into the scene.
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
        bool replaced = euclidScenes::has(scene);
        euclidScenes::save(scene);
        snprintf(text, sizeof(text), "%s Scene %c", replaced ? "Saved over" : "Saved", letter);
        EP->showMessage(text);
        return;
    }

    if (!euclidScenes::recall(scene))
    {
        snprintf(text, sizeof(text), "Scene %c is empty: Shift + key saves it", letter);
        EP->showMessage(text);
        return;
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
