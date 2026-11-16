#include "euclidParams/euclidParams.h"

#include "euclid/euclidPlatform.h"
#include "euclid/euclidSets.h"

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
static void functDelete();
static void functInsert();
static void functPads(uint8_t pad, uint8_t state, int16_t velo);

static constexpr uint8_t kAuditionVelocity = 100;

void cEuclidParams::update()
{
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
        else
            refreshGlobal();
        refreshChannels();
        refreshRing();
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
    ip->setButtonObj(interfaceButtonSamplePlay, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonSampleEdit, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonSampleRec, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonSampleLoad, buttonPress, functSwitchModule);
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
    padsBacklight.clearGlobalPads(1, 1, 1);
}

void cEuclidParams::setDefaultScreenFunct()
{
    ip->clearButtonsRange(interfaceButton0, interfaceButton7);
    ip->clearAllPots();

    ip->setButtonObj(interfaceButtonPlay, buttonPress, functPlayAction);

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

    // Outside the params view the encoder moves one item at a time (and in
    // the notes view Shift means octaves), so the stock Shift acceleration
    // is only on for params.
    ip->setPotObj(interfacePot0, functEncoder, view == View::params);
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
    else
        refreshNotes();
    refreshFrame();
    refreshChannels();
    refreshRing();
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
}

void cEuclidParams::moveNoteCursor(int8_t delta)
{
    euclid::Rhythm r = euclidEngine.getRhythm(selectedChannel);
    int next = static_cast<int>(noteCursor) + delta;
    if (next < 0)
        next = 0;
    if (next >= r.noteCount)
        next = r.noteCount - 1;
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
        case paramKey:
        {
            // One row cycles Follow, +1 ... +11, Own.
            int index = x.keyMode == euclid::KeyMode::follow   ? 0
                        : x.keyMode == euclid::KeyMode::offset ? x.keyOffset
                                                               : 12;
            index = clampInt(index + step, 0, 12);
            if (index == 0)
                x.keyMode = euclid::KeyMode::follow;
            else if (index == 12)
                x.keyMode = euclid::KeyMode::own;
            else
            {
                x.keyMode = euclid::KeyMode::offset;
                x.keyOffset = static_cast<uint8_t>(index);
            }
            euclidEngine.setExpression(ch, x);
            break;
        }
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
    refreshChannels();
    refreshRing();
}

//==============================================================================================
// Note list editing

void cEuclidParams::addNote(uint8_t note)
{
    euclid::Rhythm r = euclidEngine.getRhythm(selectedChannel);

    // After a clear the list holds one leftover note (a channel can't be
    // empty); the first pad replaces it instead of adding after it.
    if (replaceNextNote)
    {
        replaceNextNote = false;
        r.notes[0] = note;
        euclidEngine.setNotes(selectedChannel, r.notes, 1);
        noteCursor = 0;
    }
    else
    {
        if (r.noteCount >= kNoteSlots)
            return;
        r.notes[r.noteCount] = note;
        euclidEngine.setNotes(selectedChannel, r.notes, r.noteCount + 1);
        noteCursor = r.noteCount;
    }
    refreshNotes();
    refreshFrame();
}

void cEuclidParams::transposeNote(int16_t delta)
{
    euclid::Rhythm r = euclidEngine.getRhythm(selectedChannel);
    if (noteCursor >= r.noteCount)
        return;
    replaceNextNote = false;
    r.notes[noteCursor] = static_cast<uint8_t>(clampInt(r.notes[noteCursor] + delta, 0, 127));
    euclidEngine.setNotes(selectedChannel, r.notes, r.noteCount);
    refreshNotes();
}

void cEuclidParams::deleteNote()
{
    euclid::Rhythm r = euclidEngine.getRhythm(selectedChannel);
    if (r.noteCount <= 1 || noteCursor >= r.noteCount)
        return; // a channel always keeps at least one note; Shift+Delete clears

    for (uint8_t i = noteCursor; i + 1 < r.noteCount; i++)
        r.notes[i] = r.notes[i + 1];
    euclidEngine.setNotes(selectedChannel, r.notes, r.noteCount - 1);
    if (noteCursor >= r.noteCount - 1)
        noteCursor = r.noteCount - 2;
    refreshNotes();
    refreshFrame();
}

void cEuclidParams::clearNotes()
{
    euclid::Rhythm r = euclidEngine.getRhythm(selectedChannel);
    euclidEngine.setNotes(selectedChannel, r.notes, 1);
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
        default: return;
    }
    refreshGlobal();
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
    euclid::Rhythm r = euclidEngine.getRhythm(selectedChannel);
    if (r.noteCount >= kNoteSlots || noteCursor >= r.noteCount)
        return;
    replaceNextNote = false;

    for (uint8_t i = r.noteCount; i > noteCursor + 1; i--)
        r.notes[i] = r.notes[i - 1];
    r.notes[noteCursor + 1] = r.notes[noteCursor];
    euclidEngine.setNotes(selectedChannel, r.notes, r.noteCount + 1);
    noteCursor++;
    refreshNotes();
    refreshFrame();
}

//==============================================================================================
static void functEncoder(int16_t value)
{
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
    if (EP->view == cEuclidParams::View::global)
    {
        if (EP->selectedGlobal > 0)
            EP->selectedGlobal--;
        EP->refreshFrame();
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
    if (EP->view == cEuclidParams::View::global)
    {
        if (EP->selectedGlobal + 1 < cEuclidParams::globalCount)
            EP->selectedGlobal++;
        EP->refreshFrame();
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
    if (EP->buttons->isButtonPressed(interfaceButtonShift))
        EP->clearNotes();
    else
        EP->deleteNote();
}

static void functInsert()
{
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

        uint8_t note = mtPadBoard.getNoteFromPad(pad);

        // Let the pad be heard on the channel's MIDI output.
        euclid::Expression x = euclidEngine.getExpression(EP->selectedChannel);
        if (EP->heldPadNote[pad] >= 0)
            sendMidiNoteOff(static_cast<uint8_t>(EP->heldPadNote[pad]), 0, EP->heldPadChannel[pad], false);
        sendMidiNoteOn(note, kAuditionVelocity, x.midiChannel, false);
        EP->heldPadNote[pad] = note;
        EP->heldPadChannel[pad] = x.midiChannel;

        if (EP->view == cEuclidParams::View::notes)
            EP->addNote(note);
    }
    else if (state == buttonRelease)
    {
        padsBacklight.setFrontLayer(0, 0, pad);

        if (EP->heldPadNote[pad] >= 0)
        {
            sendMidiNoteOff(static_cast<uint8_t>(EP->heldPadNote[pad]), 0, EP->heldPadChannel[pad], false);
            EP->heldPadNote[pad] = -1;
        }
    }
}

static void functChannelButton(uint8_t button, uint8_t state)
{
    if (state != buttonPress)
        return;

    uint8_t ch = button - interfaceButton0;

    if (EP->view == cEuclidParams::View::sets)
    {
        EP->setSoftKey(ch);
        return;
    }

    if (EP->buttons->isButtonPressed(interfaceButtonShift))
    {
        EP->toggleMute(ch);
    }
    else
    {
        EP->selectChannel(ch);
    }
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

static void functSwitchModule(uint8_t button)
{
    EP->eventFunct(eventSwitchModule, EP, &button, 0);
}
