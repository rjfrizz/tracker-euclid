#include "euclidParams/euclidParams.h"

#include "euclid/euclidEngine.h"

#include <stdio.h>

namespace
{
constexpr int16_t kListX = 430;
constexpr int16_t kListW = 360;
constexpr int16_t kListY = 40;
constexpr int16_t kRowH = 40;

const char* const kParamNames[cEuclidParams::paramCount] = {
    "Steps", "Pulses", "Rotation", "Division", "Velocity", "Gate", "Chance", "MIDI channel", "Channel",
};

uint16_t framePlaces[cEuclidParams::paramCount][4];
} // namespace

void cEuclidParams::initDisplayControls()
{
    const Theme& theme = display.getTheme();
    strControlProperties prop;

    // Ring, left half.
    prop.x = 10;
    prop.y = 32;
    prop.w = 400;
    prop.h = 388;
    prop.style = controlStyleShow | controlStyleFont4;
    prop.data = &ringData;
    ring = display.createControl<cEuclidRing>(&prop);

    // Settings list, right half.
    for (uint8_t i = 0; i < paramCount; i++)
    {
        int16_t y = kListY + i * kRowH;

        strControlProperties p;
        p.colors = theme.activeLabelsColors;
        p.style = controlStyleShow | controlStyleCenterY | controlStyleFont3;
        p.x = kListX + 12;
        p.y = y;
        p.w = kListW / 2;
        p.h = kRowH;
        p.text = kParamNames[i];
        paramName[i] = display.createControl<cLabel>(&p);

        p.style = controlStyleShow | controlStyleCenterY | controlStyleRightX | controlStyleFont3;
        p.x = kListX + kListW - 12;
        p.text = valueText[i];
        paramValue[i] = display.createControl<cLabel>(&p);

        framePlaces[i][0] = kListX;
        framePlaces[i][1] = y + 2;
        framePlaces[i][2] = kListW;
        framePlaces[i][3] = kRowH - 4;
        frameData.places[i] = &framePlaces[i][0];
    }
    frameData.placesCount = paramCount;
    frameData.startPlace = selectedParam;
    frameData.multiSelActiveNum = 0;

    strControlProperties fp;
    fp.style = 0;
    fp.value = selectedParam;
    fp.data = &frameData;
    frame = display.createControl<cFrame>(&fp);

    // Channels over the 8 screen keys.
    for (uint8_t i = 0; i < 8; i++)
    {
        strControlProperties p;
        p.value = 1; // two lines
        p.colors = theme.activeLabelsColors;
        p.style = controlStyleShow | controlStyleCenterX | controlStyleCenterY | controlStyleFont3;
        p.x = (800 / 8) * i + (800 / 16);
        p.w = 800 / 8 - 6;
        p.y = 424;
        p.h = 55;
        channelLabel[i] = display.createControl<cLabel>(&p);
    }

    strControlProperties bg;
    bg.value = 255;
    bg.style = controlStyleNoTransparency | controlStyleShow;
    bg.x = 0;
    bg.w = 800;
    bg.y = 425;
    bg.h = 54;
    bgLabel = display.createControl<cBgLabel>(&bg);
}

void cEuclidParams::showDefaultScreen()
{
    titleBar->show();
    refreshTitle();
    refreshParams();
    refreshChannels();
    refreshFrame();
    refreshRing();
    display.refreshControl(bgLabel);
    display.synchronizeRefresh();
}

void cEuclidParams::refreshTitle()
{
    snprintf(titleText, sizeof(titleText), "Euclid  Ch %d", selectedChannel + 1);
    titleBar->setTitle(titleText);
}

void cEuclidParams::refreshFrame()
{
    display.setControlValue(frame, selectedParam);
    display.setControlShow(frame);
    display.refreshControl(frame);
}

void cEuclidParams::refreshParam(uint8_t param)
{
    euclid::Rhythm r = euclidEngine.getRhythm(selectedChannel);
    euclid::Expression x = euclidEngine.getExpression(selectedChannel);
    char* t = valueText[param];
    const size_t n = sizeof(valueText[param]);

    switch (param)
    {
        case paramSteps: snprintf(t, n, "%d", r.steps); break;
        case paramPulses: snprintf(t, n, "%d", r.pulses); break;
        case paramRotation: snprintf(t, n, "%d", r.rotation); break;
        case paramDivision: snprintf(t, n, "%s", euclid::divisionName(r.division)); break;
        case paramVelocity: snprintf(t, n, "%d", x.velocity); break;
        case paramGate: snprintf(t, n, "%d%%", x.gate); break;
        case paramChance: snprintf(t, n, "%d%%", x.probability); break;
        case paramMidiChannel: snprintf(t, n, "%d", x.midiChannel); break;
        case paramEnabled: snprintf(t, n, "%s", x.enabled ? "On" : "Off"); break;
        default: t[0] = 0; break;
    }

    display.setControlText(paramValue[param], t);
    display.refreshControl(paramValue[param]);
}

void cEuclidParams::refreshParams()
{
    for (uint8_t i = 0; i < paramCount; i++)
    {
        display.refreshControl(paramName[i]);
        refreshParam(i);
    }
}

void cEuclidParams::refreshChannels()
{
    const Theme& theme = display.getTheme();

    for (uint8_t i = 0; i < 8; i++)
    {
        euclid::Rhythm r = euclidEngine.getRhythm(i);
        euclid::Expression x = euclidEngine.getExpression(i);

        snprintf(channelText[i], sizeof(channelText[i]), "Ch %d", i + 1);
        if (!x.enabled)
            snprintf(channelText2[i], sizeof(channelText2[i]), "off");
        else if (x.muted)
            snprintf(channelText2[i], sizeof(channelText2[i]), "mute");
        else
            snprintf(channelText2[i], sizeof(channelText2[i]), "%d/%d", r.pulses, r.steps);

        const uint32_t* colors = theme.inactiveLabelsColors;
        if (x.enabled && x.muted)
            colors = theme.performMutedTrackLabelsColors;
        else if (i == selectedChannel)
            colors = theme.activeLabelsColors;

        display.setControlColors(channelLabel[i], colors);
        display.setControlText(channelLabel[i], channelText[i]);
        display.setControlText2(channelLabel[i], channelText2[i]);
        display.refreshControl(channelLabel[i]);
    }
}

void cEuclidParams::refreshRing()
{
    euclid::Rhythm active = euclidEngine.getActiveRhythm(selectedChannel);
    euclid::Rhythm pending = euclidEngine.getRhythm(selectedChannel);
    euclid::Expression x = euclidEngine.getExpression(selectedChannel);
    bool running = euclidEngine.isRunning();
    bool hasPending = euclidEngine.hasPendingChange(selectedChannel);

    ringData.steps = active.steps;
    ringData.pulses = active.pulses;
    ringData.rotation = active.rotation;
    ringData.playhead = running ? static_cast<int8_t>(euclidEngine.getCurrentStep(selectedChannel)) : -1;
    ringData.enabled = x.enabled;
    ringData.muted = x.muted;

    ringData.hasPending = hasPending;
    ringData.pendingSteps = pending.steps;
    ringData.pendingPulses = pending.pulses;
    ringData.pendingRotation = pending.rotation;

    snprintf(ringText, sizeof(ringText), "E(%d,%d)", active.pulses, active.steps);
    ringData.centerText = ringText;

    if (hasPending)
    {
        snprintf(ringText2, sizeof(ringText2), "next bar: E(%d,%d)", pending.pulses, pending.steps);
        ringData.centerText2 = ringText2;
    }
    else if (!x.enabled)
    {
        snprintf(ringText2, sizeof(ringText2), "off");
        ringData.centerText2 = ringText2;
    }
    else if (x.muted)
    {
        snprintf(ringText2, sizeof(ringText2), "muted");
        ringData.centerText2 = ringText2;
    }
    else
    {
        ringData.centerText2 = nullptr;
    }

    display.setControlData(ring, &ringData);
    display.refreshControl(ring);

    lastPlayhead = ringData.playhead;
    lastPending = hasPending;
    lastRunning = running;
}
