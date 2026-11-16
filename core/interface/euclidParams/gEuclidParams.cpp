#include "euclidParams/euclidParams.h"

#include "mtStructs.h"

#include <stdio.h>

namespace
{
const char* const kParamNames[cEuclidParams::paramCount] = {
    "Steps", "Pulses", "Rotation", "Division", "Note order",
    "Velocity", "Gate", "Chance", "MIDI channel", "Channel",
};

const char* const kDirectionNames[] = { "Forward", "Backward", "Ping-pong", "Random" };

// Each line must fit the 360 px panel in the 17 px font: about 34 characters.
const char* const kNoteHelp[] = {
    "Pads: add a note",
    "Encoder: transpose, Shift: octave",
    "Delete: remove, Insert: repeat",
    "Shift + Delete: clear all",
};

static_assert(cEuclidParams::paramCount <= strEuclidPanelData::kMaxRows, "too many rows");
static_assert(cEuclidParams::kNoteSlots <= strEuclidPanelData::kMaxCells, "too many cells");
} // namespace

// Controls on this screen: ring, panel, 8 channel labels and the bar behind
// them. The display has 65 slots shared with the title bar and popups, so
// the lists are drawn inside the panel rather than as one label per row.
void cEuclidParams::initDisplayControls()
{
    const Theme& theme = display.getTheme();

    strControlProperties prop;
    prop.x = 10;
    prop.y = 32;
    prop.w = 400;
    prop.h = 388;
    prop.style = controlStyleShow | controlStyleFont4;
    prop.data = &ringData;
    ring = display.createControl<cEuclidRing>(&prop);

    strControlProperties pp;
    pp.x = 430;
    pp.y = 40;
    pp.w = 360;
    pp.h = 380;
    pp.style = controlStyleShow;
    pp.data = &panelData;
    panel = display.createControl<cEuclidPanel>(&pp);

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

    for (uint8_t i = 0; i < paramCount; i++)
    {
        panelData.names[i] = kParamNames[i];
        panelData.values[i] = valueText[i];
    }
    panelData.rows = paramCount;
    panelData.columns = kNoteColumns;
    panelData.cells = kNoteSlots;
    panelData.header = noteHeaderText;
    for (uint8_t i = 0; i < strEuclidPanelData::kMaxHelpLines; i++)
        panelData.help[i] = kNoteHelp[i];
}

void cEuclidParams::showDefaultScreen()
{
    titleBar->show();
    setView(view);
    refreshChannels();
    refreshRing();
    display.refreshControl(bgLabel);
    display.synchronizeRefresh();
}

void cEuclidParams::setView(View v)
{
    view = v;
    panelData.mode = view == View::params ? strEuclidPanelData::Mode::list
                                          : strEuclidPanelData::Mode::grid;
    refreshTitle();
    if (view == View::params)
        refreshParams();
    else
        refreshNotes();
    display.synchronizeRefresh();
}

void cEuclidParams::refreshTitle()
{
    snprintf(titleText, sizeof(titleText), "%s  Ch %d", view == View::params ? "Params" : "Notes",
             selectedChannel + 1);
    titleBar->setTitle(titleText);
}

void cEuclidParams::refreshFrame()
{
    panelData.focusRow = static_cast<int8_t>(selectedParam);
    panelData.cursorCell = static_cast<int8_t>(noteCursor);
    display.setControlData(panel, &panelData);
    display.refreshControl(panel);
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
        case paramNoteOrder:
            snprintf(t, n, "%s", kDirectionNames[static_cast<uint8_t>(r.direction) % 4]);
            break;
        case paramVelocity: snprintf(t, n, "%d", x.velocity); break;
        case paramGate: snprintf(t, n, "%d%%", x.gate); break;
        case paramChance: snprintf(t, n, "%d%%", x.probability); break;
        case paramMidiChannel: snprintf(t, n, "%d", x.midiChannel); break;
        case paramEnabled: snprintf(t, n, "%s", x.enabled ? "On" : "Off"); break;
        default: t[0] = 0; break;
    }

    if (view == View::params)
        refreshFrame();
}

void cEuclidParams::refreshParams()
{
    for (uint8_t i = 0; i < paramCount; i++)
        refreshParam(i);
}

void cEuclidParams::refreshNotes()
{
    euclid::Rhythm r = euclidEngine.getRhythm(selectedChannel);
    int8_t next = euclidEngine.isRunning()
                    ? static_cast<int8_t>(euclidEngine.getNextNoteIndex(selectedChannel))
                    : -1;
    lastNextNote = next;

    if (view != View::notes)
        return;

    if (noteCursor >= r.noteCount)
        noteCursor = r.noteCount - 1;

    if (replaceNextNote)
        snprintf(noteHeaderText, sizeof(noteHeaderText), "Cleared: tap pads to enter notes");
    else
        snprintf(noteHeaderText, sizeof(noteHeaderText), "%d of %d notes", r.noteCount, kNoteSlots);

    for (uint8_t i = 0; i < kNoteSlots; i++)
        panelData.cellText[i] = i < r.noteCount ? mtNotes[r.notes[i] & 127] : nullptr;
    panelData.usedCells = r.noteCount;
    panelData.playingCell = next;

    refreshFrame();
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
    ringData.playhead =
      running ? static_cast<int8_t>(euclidEngine.getCurrentStep(selectedChannel)) : -1;
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
