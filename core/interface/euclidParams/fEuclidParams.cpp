#include "euclidParams/euclidParams.h"

#include "mtConfig.h"
#include "mtMidi.h"
#include "mtPadBoard.h"
#include "mtPadsBacklight.h"
#include "mtSequencer.h"

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
static void functDelete();
static void functInsert();
static void functPads(uint8_t pad, uint8_t state, int16_t velo);

static constexpr uint8_t kAuditionVelocity = 100;

void cEuclidParams::update()
{
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
    ip->setButtonObj(interfaceButtonFile, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonConfig, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonMaster, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonSamplePlay, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonSampleEdit, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonSampleRec, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonSampleLoad, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonSong, buttonPress, functSwitchModule);
    ip->setButtonObj(interfaceButtonPattern, buttonPress, functSwitchModule);

    ip->setButtonObj(interfaceButtonParams, buttonPress, functParamsView);
    ip->setButtonObj(interfaceButtonNote, buttonPress, functNotesView);

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

    // In the notes view Shift + encoder moves by octaves, so the stock
    // Shift acceleration is turned off there.
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
    if (EP->view == cEuclidParams::View::notes)
        EP->moveNoteCursor(-static_cast<int8_t>(cEuclidParams::kNoteColumns));
    else
        EP->moveFocus(-1);
}

static void functDown()
{
    if (EP->view == cEuclidParams::View::notes)
        EP->moveNoteCursor(cEuclidParams::kNoteColumns);
    else
        EP->moveFocus(1);
}

static void functLeft()
{
    if (EP->view == cEuclidParams::View::notes)
    {
        EP->moveNoteCursor(-1);
        return;
    }
    EP->selectChannel(EP->selectedChannel == 0 ? euclid::kChannels - 1 : EP->selectedChannel - 1);
}

static void functRight()
{
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
