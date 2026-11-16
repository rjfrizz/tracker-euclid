#include "euclidParams/euclidParams.h"

#include "euclid/euclidEngine.h"
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

void cEuclidParams::update()
{
    bool running = euclidEngine.isRunning();
    int8_t playhead = running ? static_cast<int8_t>(euclidEngine.getCurrentStep(selectedChannel)) : -1;
    bool pending = euclidEngine.hasPendingChange(selectedChannel);

    if (playhead != lastPlayhead || pending != lastPending || running != lastRunning)
    {
        bool pendingCleared = lastPending && !pending;
        refreshRing();
        if (pendingCleared)
            refreshChannels();
    }
}

void cEuclidParams::start([[maybe_unused]] uint32_t options)
{
    moduleRefresh = 1;

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

    lastPlayhead = -2;
    showDefaultScreen();
    setDefaultScreenFunct();
}

void cEuclidParams::stop()
{
    moduleRefresh = 0;
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

    for (uint8_t b = interfaceButton0; b <= interfaceButton7; b++)
    {
        ip->setButtonObj(b, functChannelButton);
    }

    ip->setPotObj(interfacePot0, functEncoder);
}

void cEuclidParams::selectChannel(uint8_t ch)
{
    if (ch >= euclid::kChannels)
        return;
    selectedChannel = ch;
    refreshTitle();
    refreshParams();
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
            int d = clampInt(static_cast<int>(r.division) + (delta > 0 ? 1 : -1), 0,
                             static_cast<int>(euclid::Division::count) - 1);
            euclidEngine.setDivision(ch, static_cast<euclid::Division>(d));
            rhythmChanged = true;
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
static void functEncoder(int16_t value)
{
    EP->changeParam(value);
}

static void functUp()
{
    EP->moveFocus(-1);
}

static void functDown()
{
    EP->moveFocus(1);
}

static void functLeft()
{
    EP->selectChannel(EP->selectedChannel == 0 ? euclid::kChannels - 1 : EP->selectedChannel - 1);
}

static void functRight()
{
    EP->selectChannel((EP->selectedChannel + 1) % euclid::kChannels);
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
