#include "euclidParams/euclidParams.h"

#include "euclid/euclidPlatform.h"
#include "euclid/euclidSets.h"
#include "Platform.h"
#include "TrackerImpl.h"
#include "mtSequencer.h"
#include "mtStructs.h"

#include <stdio.h>

namespace
{
const char* const kParamNames[cEuclidParams::paramCount] = {
    "Name",      "Steps",    "Pulses",   "Rotation", "Division", "Mode",   "Chord",  "Chord chance", "Note order",
    "Key",       "Own root", "Own scale", "Octave",  "Range",  "Transpose", "Velocity",
    "Gate",      "Chance",   "MIDI channel", "Channel",
};

const char* const kGlobalNames[cEuclidParams::globalCount] = { "Tempo", "Root", "Scale", "Launch", "Freeze", "Layer" };


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
    // Params, Global and Mod are lists of settings; Notes and Sets are grids.
    panelData.mode = isListView() ? strEuclidPanelData::Mode::list : strEuclidPanelData::Mode::grid;
    panelData.cells = view == View::sets ? euclid::kSetSlots : kNoteSlots;
    panelData.columns = kNoteColumns; // the Shift steps use 8, set in refreshShift
    panelData.header = noteHeaderText;
    const char* const* help = view == View::sets ? kSetsHelp : kNoteHelp;
    for (uint8_t i = 0; i < strEuclidPanelData::kMaxHelpLines; i++)
        panelData.help[i] = help[i];

    // Point the panel at the right list (Mod and Synth fill their own rows).
    if (view == View::mod)
        buildModRows();
    if (view == View::synth)
    {
        panelData.rows = 0;
        panelData.meterCount = 0;
        panelData.firstRow = 0;
        panelData.dimRows = 0;
        panelData.stagedRows = 0;
        refreshTitle();
        refreshSynth();
        refreshChannels();
        refreshRing();
        refreshPads();
        display.synchronizeRefresh();
        return;
    }
    panelData.rows = listRowCount();
    panelData.meterCount = 0;
    for (uint8_t i = 0; i < strEuclidPanelData::kMaxRows; i++)
    {
        panelData.names[i] = nullptr;
        panelData.values[i] = nullptr;
    }
    for (uint8_t i = 0; i < panelData.rows; i++)
    {
        if (view == View::global)
        {
            panelData.names[i] = kGlobalNames[i];
            panelData.values[i] = globalText[i];
        }
        else if (view == View::mod)
        {
            continue;
        }
        else
        {
            panelData.names[i] = kParamNames[i];
            panelData.values[i] = valueText[i];
        }
    }
    panelData.firstRow = 0;
    panelData.dimRows = 0;
    panelData.stagedRows = 0;

    refreshTitle();
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
    refreshChannels();
    refreshRing();
    refreshPads();
    display.synchronizeRefresh();
}

void cEuclidParams::refreshTitle()
{
    // A freeze message, or one on its way, takes over the title for a while.
    uint32_t now = TrackerImpl::getInstance()->getPlatform()->getTimer()->millis();
    if (freezePending || static_cast<int32_t>(messageUntil - now) > 0)
    {
        snprintf(titleText, sizeof(titleText), "%s",
                 freezePending ? "Freezing at the end of the bar..." : message);
        titleBar->setTitle(titleText);
        return;
    }

    const char* hint = euclidEngine.hasAnyStagedChange() ? "   -  Rec to apply" : "";
    if (view == View::sets)
        snprintf(titleText, sizeof(titleText), "Sets%s", hint);
    else if (view == View::global)
        snprintf(titleText, sizeof(titleText), "Global%s", hint);
    else if (view == View::mod)
        snprintf(titleText, sizeof(titleText), "Mod %c  Ch %d%s", 'A' + selectedMod, selectedChannel + 1, hint);
    else if (view == View::shift)
        snprintf(titleText, sizeof(titleText), "Shift%s  Ch %d%s", shiftSettings ? " settings" : "",
                 selectedChannel + 1, hint);
    else if (view == View::synth)
        snprintf(titleText, sizeof(titleText), "Synth  Ch %d%s", selectedChannel + 1,
                 euclidSynth::sending() ? "  -  sending" : hint);
    else
        snprintf(titleText, sizeof(titleText), "%s  Ch %d%s",
                 view == View::params ? "Params" : "Notes", selectedChannel + 1, hint);
    titleBar->setTitle(titleText);
}

void cEuclidParams::refreshFrame()
{
    panelData.focusRow = static_cast<int8_t>(focusedRow());
    panelData.cursorCell =
      static_cast<int8_t>(view == View::sets ? setCursor : (view == View::shift ? shiftCursor : noteCursor));
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
        case paramName:
        {
            const char* name = euclid::channelName(euclidEngine.getName(selectedChannel));
            snprintf(t, n, "%s", name != nullptr ? name : "-");
            break;
        }
        case paramSteps: snprintf(t, n, "%d", r.steps); break;
        case paramPulses: snprintf(t, n, "%d", r.pulses); break;
        case paramRotation: snprintf(t, n, "%d", r.rotation); break;
        case paramDivision: snprintf(t, n, "%s", euclid::divisionName(r.division)); break;
        case paramMode: snprintf(t, n, "%s", euclid::noteModeName(r.mode)); break;
        case paramChord: snprintf(t, n, "%s", euclid::chordTypeName(x.chord)); break;
        case paramChordChance: snprintf(t, n, "%d%%", x.chordChance); break;
        case paramNoteOrder:
            snprintf(t, n, "%s", kDirectionNames[static_cast<uint8_t>(r.direction) % 4]);
            break;
        case paramKey:
        {
            // SEQ and the generated modes each have their own key mode.
            euclid::KeyMode m = r.mode == euclid::NoteMode::seq ? x.seqKeyMode : x.keyMode;
            if (m == euclid::KeyMode::follow)
                snprintf(t, n, "Follow");
            else if (m == euclid::KeyMode::offset)
                snprintf(t, n, "+%d", x.keyOffset);
            else
                snprintf(t, n, "Own");
            break;
        }
        case paramTranspose:
            if (x.transpose == 0)
                snprintf(t, n, "0");
            else
                snprintf(t, n, "%+d", x.transpose);
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
        panelData.stagedRows = stagedRowMask();
        refreshFrame();
    }
}

// Rows whose staged value differs from the committed one: waiting for Rec.
uint32_t cEuclidParams::stagedRowMask() const
{
    uint32_t mask = 0;
    const uint8_t rows = listRowCount();
    for (uint8_t row = 0; row < rows; row++)
    {
        int staged = rowValue(row, false);
        if (staged != kNoValue && staged != rowValue(row, true))
            mask |= 1ul << row;
    }
    return mask;
}

// Rows that don't apply to the channel's current settings are dimmed.
uint32_t cEuclidParams::paramDimMask() const
{
    euclid::Rhythm r = euclidEngine.getRhythm(selectedChannel);
    euclid::Expression x = euclidEngine.getExpression(selectedChannel);
    uint32_t dim = 0;

    if (r.mode == euclid::NoteMode::seq)
    {
        // SEQ uses Key (Own, Follow, +N) but not the generator's settings.
        dim |= (1ul << paramOwnRoot) | (1ul << paramOwnScale) | (1ul << paramOctave) |
               (1ul << paramRange);
    }
    else
    {
        dim |= 1ul << paramNoteOrder;
        if (x.keyMode != euclid::KeyMode::own)
            dim |= (1ul << paramOwnRoot) | (1ul << paramOwnScale);
    }

    // Arps always use the chord's tones, so chance doesn't apply.
    bool arp = r.mode == euclid::NoteMode::arpUp || r.mode == euclid::NoteMode::arpUpDown;
    if (x.chord == euclid::ChordType::single || arp)
        dim |= 1ul << paramChordChance;
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
    euclid::Key live = euclidEngine.getLiveGlobalKey();
    euclid::Key active = euclidEngine.getActiveGlobalKey();

    if (sequencer.isSetInternalClock())
        snprintf(globalText[globalTempo], sizeof(globalText[globalTempo]), "%d BPM",
                 static_cast<int>(mtProject.values.globalTempo + 0.5f));
    else
        snprintf(globalText[globalTempo], sizeof(globalText[globalTempo]), "Ext %d",
                 static_cast<int>(sequencer.getCurrentTempo() + 0.5f));

    // A staged value is drawn in orange (stagedRowMask). A committed one
    // waiting for the launch point is shown with an arrow from what's playing.
    if (k.root == live.root && live.root != active.root)
        snprintf(globalText[globalRoot], sizeof(globalText[globalRoot]), "%s > %s",
                 euclid::rootName(active.root), euclid::rootName(live.root));
    else
        snprintf(globalText[globalRoot], sizeof(globalText[globalRoot]), "%s", euclid::rootName(k.root));
    snprintf(globalText[globalScale], sizeof(globalText[globalScale]), "%s%s",
             k.scale == live.scale && live.scale != active.scale ? "> " : "", euclid::scaleName(k.scale));
    snprintf(globalText[globalLaunch], sizeof(globalText[globalLaunch]), "%s",
             euclid::launchName(euclidEngine.getLaunch()));
    {
        uint8_t bars = euclid::freezeLengthBars(euclidEngine.getFreezeLength());
        snprintf(globalText[globalFreeze], sizeof(globalText[globalFreeze]), "%d bar%s", bars,
                 bars == 1 ? "" : "s");
    }
    snprintf(globalText[globalLayer], sizeof(globalText[globalLayer]), "%s",
             euclidEngine.getLayer() ? "On" : "Off");

    panelData.dimRows = sequencer.isSetInternalClock() ? 0 : (1u << globalTempo);
    panelData.stagedRows = stagedRowMask();
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

    euclid::Rhythm live = euclidEngine.getLiveRhythm(selectedChannel);
    bool notesStaged = live.noteCount != r.noteCount || live.tieMask != r.tieMask;
    for (uint8_t i = 0; i < r.noteCount && !notesStaged; i++)
        notesStaged = live.notes[i] != r.notes[i];

    const uint8_t entries = euclid::entryCount(r);
    if (noteCursor >= entries)
        noteCursor = entries - 1;

    if (r.mode != euclid::NoteMode::seq)
        snprintf(noteHeaderText, sizeof(noteHeaderText), "Used when Mode is SEQ");
    else if (notesStaged)
        snprintf(noteHeaderText, sizeof(noteHeaderText), "Edited: Rec to apply");
    else if (replaceNextNote)
        snprintf(noteHeaderText, sizeof(noteHeaderText), "Cleared: tap pads to add");
    else if (euclidEngine.getExpression(selectedChannel).seqKeyMode != euclid::KeyMode::own)
        snprintf(noteHeaderText, sizeof(noteHeaderText), "%d notes, home %s %s", r.noteCount,
                 euclid::rootName(r.homeRoot), euclid::scaleName(r.homeScale));
    else
        snprintf(noteHeaderText, sizeof(noteHeaderText), "%d of %d notes", r.noteCount, kNoteSlots);

    // One cell per entry: its lowest note, plus how many more for a chord.
    for (uint8_t e = 0; e < kNoteSlots; e++)
    {
        if (e >= entries)
        {
            panelData.cellText[e] = nullptr;
            continue;
        }
        const uint8_t start = euclid::entryStart(r, e);
        const uint8_t len = euclid::entryLength(r, e);
        uint8_t low = r.notes[start];
        for (uint8_t i = 1; i < len; i++)
            if (r.notes[start + i] < low)
                low = r.notes[start + i];
        if (len > 1)
            snprintf(noteCellText[e], sizeof(noteCellText[e]), "%s+%d", mtNotes[low & 127], len - 1);
        else
            snprintf(noteCellText[e], sizeof(noteCellText[e]), "%s", mtNotes[low & 127]);
        panelData.cellText[e] = noteCellText[e];
    }
    panelData.usedCells = entries;
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

        const char* name = euclid::channelName(euclidEngine.getName(i));
        if (name != nullptr)
            snprintf(channelText[i], sizeof(channelText[i]), "%d %s", i + 1, name);
        else
            snprintf(channelText[i], sizeof(channelText[i]), "Ch %d", i + 1);
        if (!x.enabled)
            snprintf(channelText2[i], sizeof(channelText2[i]), "off");
        else if (x.muted)
            snprintf(channelText2[i], sizeof(channelText2[i]), "mute");
        else
            snprintf(channelText2[i], sizeof(channelText2[i]), "%d/%d%s", r.pulses, r.steps,
                     euclidEngine.hasStagedChange(i) ? " *" : "");

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
    const uint8_t ch = selectedChannel;
    euclid::Rhythm active = euclidEngine.getActiveRhythm(ch);
    euclid::Rhythm live = euclidEngine.getLiveRhythm(ch);
    euclid::Rhythm staged = euclidEngine.getRhythm(ch);
    euclid::Expression x = euclidEngine.getExpression(ch);
    bool running = euclidEngine.isRunning();
    bool hasPending = euclidEngine.hasPendingChange(ch);
    bool hasStaged = euclidEngine.hasStagedChange(ch);

    ringData.steps = active.steps;
    ringData.pulses = active.pulses;
    ringData.rotation = active.rotation;
    ringData.playhead = running ? static_cast<int8_t>(euclidEngine.getCurrentStep(ch)) : -1;
    ringData.enabled = x.enabled;
    ringData.muted = x.muted;

    // Inner ring: the rhythm waiting for Rec, or the one waiting for the
    // launch point, when it differs from what's playing.
    const euclid::Rhythm& next = hasStaged ? staged : live;
    ringData.hasPending = next.steps != active.steps || next.pulses != active.pulses ||
                          next.rotation != active.rotation;
    ringData.pendingSteps = next.steps;
    ringData.pendingPulses = next.pulses;
    ringData.pendingRotation = next.rotation;

    snprintf(ringText, sizeof(ringText), "E(%d,%d)", active.pulses, active.steps);
    ringData.centerText = ringText;

    // On the Mod page the inner ring is the modulator's rhythm.
    if (view == View::mod)
    {
        const euclid::Mod& m = euclidEngine.getExpression(ch).mod[selectedMod];
        ringData.hasPending = m.dest != euclid::ModDest::off && euclid::isEuclidShape(m.shape);
        ringData.pendingSteps = m.steps;
        ringData.pendingPulses = m.pulses;
        ringData.pendingRotation = euclidEngine.getModRotation(ch, selectedMod);
        lastModRotation = ringData.pendingRotation;
        snprintf(ringText2, sizeof(ringText2), "Mod %c: %s", 'A' + selectedMod, euclid::modDestName(m.dest));
        ringData.centerText2 = ringText2;
        display.setControlData(ring, &ringData);
        display.refreshControl(ring);
        lastPlayhead = ringData.playhead;
        lastPending = hasPending;
        lastRunning = running;
        return;
    }

    if (hasStaged)
    {
        if (ringData.hasPending)
            snprintf(ringText2, sizeof(ringText2), "Rec: E(%d,%d)", staged.pulses, staged.steps);
        else
            snprintf(ringText2, sizeof(ringText2), "Rec to apply");
        ringData.centerText2 = ringText2;
    }
    else if (hasPending)
    {
        snprintf(ringText2, sizeof(ringText2), "next %s: E(%d,%d)",
                 euclidEngine.getLaunch() == euclid::Launch::beat ? "beat" : "bar", live.pulses,
                 live.steps);
        ringData.centerText2 = ringText2;
    }
    else if (view == View::shift)
    {
        const euclid::Shift& sh = x.shift;
        if (sh.on)
            snprintf(ringText2, sizeof(ringText2), "Shift: %d steps", sh.length);
        else
            snprintf(ringText2, sizeof(ringText2), "Shift off");
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
    else if (staged.mode != euclid::NoteMode::seq || x.seqKeyMode != euclid::KeyMode::own)
    {
        euclid::Key k = euclidEngine.getChannelKey(ch);
        snprintf(ringText2, sizeof(ringText2), "%s  %s %s", euclid::noteModeName(staged.mode),
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
