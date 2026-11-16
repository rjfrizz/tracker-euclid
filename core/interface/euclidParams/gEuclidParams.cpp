#include "euclidParams/euclidParams.h"

#include "euclid/euclidPlatform.h"
#include "euclid/euclidSets.h"
#include "mtSequencer.h"
#include "mtStructs.h"

#include <stdio.h>

namespace
{
const char* const kParamNames[cEuclidParams::paramCount] = {
    "Steps",     "Pulses",   "Rotation", "Division", "Mode",   "Note order",
    "Key",       "Own root", "Own scale", "Octave",  "Range",  "Velocity",
    "Gate",      "Chance",   "MIDI channel", "Channel",
};

const char* const kGlobalNames[cEuclidParams::globalCount] = { "Tempo", "Root", "Scale" };


const char* const kDirectionNames[] = { "Forward", "Backward", "Ping-pong", "Random" };

// Each line must fit the 360 px panel in the 17 px font: about 34 characters.
const char* const kNoteHelp[] = {
    "Pads: add a note",
    "Encoder: transpose, Shift: octave",
    "Delete: remove, Insert: repeat",
    "Shift + Delete: clear all",
};

const char* const kSetsHelp[] = {
    "Arrows or encoder: pick a set",
    "Key 1: save, Key 2: load",
    "Press twice to confirm",
    "Your work also autosaves",
};

const char* const kSetSoftKeys[8] = { "Save", "Load", "", "", "", "", "", "Back" };

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

    panelData.columns = kNoteColumns;
    panelData.header = noteHeaderText;
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
    armedAction = SetAction::none;
    panelData.mode = view == View::params ? strEuclidPanelData::Mode::list
                                          : strEuclidPanelData::Mode::grid;
    panelData.cells = view == View::sets ? euclid::kSetSlots : kNoteSlots;
    const char* const* help = view == View::sets ? kSetsHelp : kNoteHelp;
    for (uint8_t i = 0; i < strEuclidPanelData::kMaxHelpLines; i++)
        panelData.help[i] = help[i];

    // Params and Global are both lists; point the panel at the right one.
    const bool global = view == View::global;
    panelData.rows = static_cast<uint8_t>(global ? static_cast<int>(globalCount) : static_cast<int>(paramCount));
    for (uint8_t i = 0; i < strEuclidPanelData::kMaxRows; i++)
    {
        panelData.names[i] = nullptr;
        panelData.values[i] = nullptr;
    }
    for (uint8_t i = 0; i < panelData.rows; i++)
    {
        panelData.names[i] = global ? kGlobalNames[i] : kParamNames[i];
        panelData.values[i] = global ? globalText[i] : valueText[i];
    }
    panelData.firstRow = 0;
    panelData.dimRows = 0;

    refreshTitle();
    if (view == View::params)
        refreshParams();
    else if (view == View::notes)
        refreshNotes();
    else if (view == View::sets)
        refreshSets();
    else
        refreshGlobal();
    refreshChannels();
    display.synchronizeRefresh();
}

void cEuclidParams::refreshTitle()
{
    if (view == View::sets)
        snprintf(titleText, sizeof(titleText), "Sets");
    else if (view == View::global)
        snprintf(titleText, sizeof(titleText), "Global");
    else
        snprintf(titleText, sizeof(titleText), "%s  Ch %d",
                 view == View::params ? "Params" : "Notes", selectedChannel + 1);
    titleBar->setTitle(titleText);
}

void cEuclidParams::refreshFrame()
{
    panelData.focusRow = static_cast<int8_t>(view == View::global ? selectedGlobal : selectedParam);
    panelData.cursorCell = static_cast<int8_t>(view == View::sets ? setCursor : noteCursor);
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
        case paramMode: snprintf(t, n, "%s", euclid::noteModeName(r.mode)); break;
        case paramNoteOrder:
            snprintf(t, n, "%s", kDirectionNames[static_cast<uint8_t>(r.direction) % 4]);
            break;
        case paramKey:
            if (x.keyMode == euclid::KeyMode::follow)
                snprintf(t, n, "Follow");
            else if (x.keyMode == euclid::KeyMode::offset)
                snprintf(t, n, "+%d", x.keyOffset);
            else
                snprintf(t, n, "Own");
            break;
        case paramOwnRoot: snprintf(t, n, "%s", euclid::rootName(x.ownRoot)); break;
        case paramOwnScale: snprintf(t, n, "%s", euclid::scaleName(x.ownScale)); break;
        case paramOctave: snprintf(t, n, "%d", x.octave); break;
        case paramRange: snprintf(t, n, "%d oct", x.range); break;
        case paramVelocity: snprintf(t, n, "%d", x.velocity); break;
        case paramGate: snprintf(t, n, "%d%%", x.gate); break;
        case paramChance: snprintf(t, n, "%d%%", x.probability); break;
        case paramMidiChannel: snprintf(t, n, "%d", x.midiChannel); break;
        case paramEnabled: snprintf(t, n, "%s", x.enabled ? "On" : "Off"); break;
        default: t[0] = 0; break;
    }

    if (view == View::params)
    {
        panelData.dimRows = paramDimMask();
        refreshFrame();
    }
}

// Rows that don't apply to the channel's current settings are dimmed.
uint16_t cEuclidParams::paramDimMask() const
{
    euclid::Rhythm r = euclidEngine.getRhythm(selectedChannel);
    euclid::Expression x = euclidEngine.getExpression(selectedChannel);
    uint16_t dim = 0;

    if (r.mode == euclid::NoteMode::seq)
    {
        dim |= (1u << paramKey) | (1u << paramOwnRoot) | (1u << paramOwnScale) |
               (1u << paramOctave) | (1u << paramRange);
    }
    else
    {
        dim |= 1u << paramNoteOrder;
        if (x.keyMode != euclid::KeyMode::own)
            dim |= (1u << paramOwnRoot) | (1u << paramOwnScale);
    }
    return dim;
}

void cEuclidParams::refreshParams()
{
    for (uint8_t i = 0; i < paramCount; i++)
        refreshParam(i);
}

void cEuclidParams::refreshGlobal()
{
    if (view != View::global)
        return;

    euclid::Key k = euclidEngine.getGlobalKey();
    euclid::Key active = euclidEngine.getActiveGlobalKey();
    bool pending = euclidEngine.hasPendingGlobalKey();

    if (sequencer.isSetInternalClock())
        snprintf(globalText[globalTempo], sizeof(globalText[globalTempo]), "%d BPM",
                 static_cast<int>(mtProject.values.globalTempo + 0.5f));
    else
        snprintf(globalText[globalTempo], sizeof(globalText[globalTempo]), "Ext %d",
                 static_cast<int>(sequencer.getCurrentTempo() + 0.5f));

    // While a change waits for the bar, show it with an arrow.
    if (pending && k.root != active.root)
        snprintf(globalText[globalRoot], sizeof(globalText[globalRoot]), "%s > %s",
                 euclid::rootName(active.root), euclid::rootName(k.root));
    else
        snprintf(globalText[globalRoot], sizeof(globalText[globalRoot]), "%s", euclid::rootName(k.root));
    snprintf(globalText[globalScale], sizeof(globalText[globalScale]), "%s%s",
             pending && k.scale != active.scale ? "> " : "", euclid::scaleName(k.scale));

    panelData.dimRows = sequencer.isSetInternalClock() ? 0 : (1u << globalTempo);
    refreshFrame();
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

    if (r.mode != euclid::NoteMode::seq)
        snprintf(noteHeaderText, sizeof(noteHeaderText), "Used when Mode is SEQ");
    else if (replaceNextNote)
        snprintf(noteHeaderText, sizeof(noteHeaderText), "Cleared: tap pads to add");
    else
        snprintf(noteHeaderText, sizeof(noteHeaderText), "%d of %d notes", r.noteCount, kNoteSlots);

    for (uint8_t i = 0; i < kNoteSlots; i++)
        panelData.cellText[i] = i < r.noteCount ? mtNotes[r.notes[i] & 127] : nullptr;
    panelData.usedCells = r.noteCount;
    panelData.dimCells = 0;
    panelData.playingCell = next;

    refreshFrame();
}

void cEuclidParams::refreshSets()
{
    if (view != View::sets)
        return;

    uint8_t current = euclidSets::currentSlot();
    uint16_t dim = 0;

    for (uint8_t i = 0; i < euclid::kSetSlots; i++)
    {
        bool saved = euclidSets::exists(i + 1);
        snprintf(setText[i], sizeof(setText[i]), saved ? "Set %d" : "%d", i + 1);
        panelData.cellText[i] = setText[i];
        if (!saved)
            dim |= 1u << i;
    }
    panelData.usedCells = euclid::kSetSlots;
    panelData.dimCells = dim;
    // The set the working copy belongs to gets the filled background.
    panelData.playingCell = current > 0 ? static_cast<int8_t>(current - 1) : -1;

    if (setStatus[0] != 0)
        snprintf(noteHeaderText, sizeof(noteHeaderText), "%s", setStatus);
    else if (current == 0)
        snprintf(noteHeaderText, sizeof(noteHeaderText), "Not saved to a set yet");
    else
        snprintf(noteHeaderText, sizeof(noteHeaderText), "Working on Set %d%s", current,
                 euclidSets::editedSinceSlot() ? " (edited)" : "");

    refreshFrame();
}

void cEuclidParams::refreshChannels()
{
    const Theme& theme = display.getTheme();

    if (view == View::sets)
    {
        // Soft keys.
        for (uint8_t i = 0; i < 8; i++)
        {
            const char* label = kSetSoftKeys[i];
            if (i == 0 && armedAction == SetAction::save)
                label = "Save?";
            if (i == 1 && armedAction == SetAction::load)
                label = "Load?";
            bool armed = (i == 0 && armedAction == SetAction::save) ||
                         (i == 1 && armedAction == SetAction::load);

            display.setControlColors(channelLabel[i], armed ? theme.performMutedTrackLabelsColors
                                                            : theme.activeLabelsColors);
            display.setControlText(channelLabel[i], label);
            display.setControlText2(channelLabel[i], "");
            display.refreshControl(channelLabel[i]);
        }
        return;
    }

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
    else if (pending.mode != euclid::NoteMode::seq)
    {
        euclid::Key k = euclidEngine.getChannelKey(selectedChannel);
        snprintf(ringText2, sizeof(ringText2), "%s  %s %s", euclid::noteModeName(pending.mode),
                 euclid::rootName(k.root), euclid::scaleName(k.scale));
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
