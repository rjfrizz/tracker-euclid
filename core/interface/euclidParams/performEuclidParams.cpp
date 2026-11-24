// The Perform page (the Performance key): live moves across all channels.
// Seven tools, a key each under the screen and a tab each across the top;
// a key turns its tool on, again off (hands free), and shows its tab, where
// its settings are (the Performance key also moves on a tab).
//   Freeze: holds the notes of the channels picked (and the clock, if
//     that's all of them); the wheel then moves each on through its next
//     notes, as many as Notes ahead, gliding (a single MIDI note bends,
//     chords move a voice at a time) or jumping; off carries on from the
//     notes it's nearest.
//   Walk, Arp, Scale: every channel in that mode from its next note (one of
//     them at a time), each starting near the note it played last.
//   Chord: every channel's single notes get the chord picked; the wheel
//     turns it through its inversions.
//   Pulses: every channel gets that many more pulses.
//   Jog: the channels picked leave the clock; the wheel moves them on. Off,
//     they snap back to the clock or carry on out of time.

#include "euclidParams/euclidParams.h"

#include "euclid/euclidEngine.h"
#include "euclid/euclidPlatform.h"
#include "mtStructs.h"

#include <stdio.h>
#include <string.h>

namespace
{
enum Tool : uint8_t
{
    toolFreeze = 0,
    toolWalk,
    toolArp,
    toolScale,
    toolChord,
    toolPulses,
    toolJog,
    toolCount
};
const char* const kToolNames[toolCount] = { "Freeze", "Walk", "Arp", "Scale", "Chord", "Pulses", "Jog" };

// Each tab's rows before its channel rows (Freeze and Jog have those).
const char* const kFreezeRows[] = { "Freeze", "Morph", "Move", "Notes ahead", "Bend range" };
const char* const kWalkRows[] = { "Walk" };
const char* const kArpRows[] = { "Arp", "Direction" };
const char* const kScaleRows[] = { "Scale", "Direction" };
const char* const kChordRows[] = { "Chord", "Type", "Inversion" };
const char* const kPulsesRows[] = { "Pulses", "Add" };
const char* const kJogRows[] = { "Jog", "Jog step", "Off" };
const char* const* const kRows[toolCount] = { kFreezeRows, kWalkRows, kArpRows, kScaleRows,
                                              kChordRows,  kPulsesRows, kJogRows };
const uint8_t kRowCounts[toolCount] = { 5, 1, 2, 2, 3, 2, 3 };
bool hasChannelRows(uint8_t tool)
{
    return tool == toolFreeze || tool == toolJog;
}
// The row a tab opens on: what's played live.
const uint8_t kFirstRow[toolCount] = { 1, 0, 1, 1, 2, 1, 0 };

// Jog: micro-steps a wheel click (1/32, 1/16, 1/8, 1/4).
const uint8_t kJogTicks[] = { 24, 48, 96, 192 };
const char* const kJogNames[] = { "1/32", "1/16", "1/8", "1/4" };
constexpr uint8_t kJogSteps = 4;

// Freeze's whole way (Notes ahead) in this many clicks.
constexpr int kMorphClicks = 20;

void noteList(char* out, size_t length, const uint8_t* notes, uint8_t count)
{
    size_t n = 0;
    out[0] = 0;
    for (uint8_t i = 0; i < count && n + 5 < length; i++)
        n += static_cast<size_t>(snprintf(out + n, length - n, i == 0 ? "%s" : " %s", mtNotes[notes[i] & 127]));
    if (count == 0)
        snprintf(out, length, "-");
}

int clampInt(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}
} // namespace

uint8_t cEuclidParams::performRowCount() const
{
    const uint8_t t = performTab < toolCount ? performTab : 0;
    return static_cast<uint8_t>(kRowCounts[t] + (hasChannelRows(t) ? euclid::kChannels : 0));
}

bool cEuclidParams::performToolOn(uint8_t tool) const
{
    if (tool == toolFreeze)
        return euclidEngine.isFrozen();
    if (tool == toolJog)
        return euclidEngine.isJogging();
    return tool < toolCount && ((performOn >> tool) & 1u);
}

// Walk, Arp or Scale: whichever is on (one at a time), or none.
void cEuclidParams::applyPerformMode()
{
    int8_t mode = -1;
    if (performOn & (1u << toolWalk))
        mode = static_cast<int8_t>(euclid::NoteMode::walk);
    else if (performOn & (1u << toolArp))
        mode = static_cast<int8_t>(performArpUpDown ? euclid::NoteMode::arpUpDown : euclid::NoteMode::arpUp);
    else if (performOn & (1u << toolScale))
        mode = static_cast<int8_t>(performScaleUpDown ? euclid::NoteMode::scaleUpDown : euclid::NoteMode::scaleUp);
    euclidEngine.setModeOverride(mode);
    euclidEngine.setChordOverride((performOn & (1u << toolChord)) ? static_cast<int8_t>(performChord) : -1);
    euclidEngine.setPulseBoost((performOn & (1u << toolPulses)) ? performPulses : 0);
}

void cEuclidParams::setPerformTool(uint8_t tool, bool on)
{
    if (tool == toolFreeze)
    {
        if (on && !euclidEngine.isFrozen())
        {
            if (!euclidEngine.isRunning())
            {
                showMessage("Play first: freeze holds what plays");
                return;
            }
            euclidFreeze();
            showMessage(euclidEngine.isFrozen() ? "Frozen: turn to morph" : "No channel playing is set to freeze");
        }
        else if (!on && euclidEngine.isFrozen())
            euclidEngine.unfreeze();
    }
    else if (tool == toolJog)
    {
        if (on && !euclidEngine.isJogging())
        {
            if (!euclidEngine.isRunning())
            {
                showMessage("Play first: jog takes channels off the clock");
                return;
            }
            showMessage(euclidEngine.startJog() ? "Jog: turn to move them" : "No channel playing is set to jog");
        }
        else if (!on && euclidEngine.isJogging())
            euclidEngine.stopJog(performJogSnap);
    }
    else if (tool < toolCount)
    {
        if (on)
        {
            // Walk, Arp and Scale are modes: one at a time.
            if (tool == toolWalk || tool == toolArp || tool == toolScale)
                performOn = static_cast<uint8_t>(performOn & ~((1u << toolWalk) | (1u << toolArp) | (1u << toolScale)));
            performOn = static_cast<uint8_t>(performOn | (1u << tool));
        }
        else
            performOn = static_cast<uint8_t>(performOn & ~(1u << tool));
        applyPerformMode();
    }
}

// A key under the screen: its tool on or off, and its tab shown.
void cEuclidParams::performKey(uint8_t key)
{
    if (key >= toolCount)
        return;
    setPerformTool(key, !performToolOn(key));
    if (performTab != key)
    {
        performTab = key;
        selectedPerformRow = kFirstRow[key];
    }
    refreshPerform();
    refreshChannels();
    refreshTitle();
}

void cEuclidParams::nextPerformTab()
{
    performTab = static_cast<uint8_t>((performTab + 1) % toolCount);
    selectedPerformRow = kFirstRow[performTab];
    refreshPerform();
}

void cEuclidParams::changePerform(int16_t delta)
{
    const int dir = delta > 0 ? 1 : -1;
    const uint8_t row = selectedPerformRow;
    const bool shift = buttons->isButtonPressed(interfaceButtonShift);

    // Live: Freeze's wheel morphs, Jog's moves the channels on.
    if (performTab == toolFreeze && euclidEngine.isFrozen())
    {
        const int click = euclidEngine.getMorphReach() * 1000 / kMorphClicks;
        euclidEngine.setMorph(euclidEngine.getMorph() + delta * (shift ? click / 5 : click));
        refreshPerform();
        return;
    }
    if (performTab == toolJog && euclidEngine.isJogging())
    {
        if (delta > 0)
            euclidEngine.jog(delta * kJogTicks[performJogStep % kJogSteps]);
        refreshPerform();
        return;
    }

    if (row == 0)
        setPerformTool(performTab, dir > 0);
    else if (hasChannelRows(performTab) && row >= kRowCounts[performTab])
    {
        // A channel row: whether the tool takes it.
        const uint8_t bit = static_cast<uint8_t>(1u << (row - kRowCounts[performTab]));
        if (performTab == toolFreeze)
        {
            const uint8_t m = euclidEngine.getFreezeMask();
            euclidEngine.setFreezeMask(static_cast<uint8_t>(dir > 0 ? (m | bit) : (m & ~bit)));
        }
        else
        {
            const uint8_t m = euclidEngine.getJogMask();
            euclidEngine.setJogMask(static_cast<uint8_t>(dir > 0 ? (m | bit) : (m & ~bit)));
        }
    }
    else
    {
        switch (performTab)
        {
            case toolFreeze:
                if (row == 1)
                    showMessage("Freeze (key 1), then turn");
                else if (row == 2)
                    euclidEngine.setMorphJump(dir > 0);
                else if (row == 3)
                    euclidEngine.setMorphReach(static_cast<uint8_t>(euclidEngine.getMorphReach() + dir));
                else if (row == 4)
                    euclidEngine.setBendRange(static_cast<uint8_t>(clampInt(euclidEngine.getBendRange() + dir, 1, 24)));
                break;
            case toolArp: performArpUpDown = dir > 0; break;
            case toolScale: performScaleUpDown = dir > 0; break;
            case toolChord:
                if (row == 1)
                    performChord = static_cast<uint8_t>(
                      clampInt(performChord + dir, 1, static_cast<int>(euclid::ChordType::count) - 1));
                else
                    euclidEngine.setChordInversion(static_cast<int8_t>(euclidEngine.getChordInversion() + dir));
                break;
            case toolPulses: performPulses = static_cast<uint8_t>(clampInt(performPulses + dir, 1, 16)); break;
            case toolJog:
                if (row == 1)
                    performJogStep = static_cast<uint8_t>(clampInt(performJogStep + dir, 0, kJogSteps - 1));
                else
                    performJogSnap = dir < 0;
                break;
            default: break;
        }
        applyPerformMode(); // heard straight away if the tool's on
    }
    refreshPerform();
    refreshChannels();
    refreshTitle();
}

void cEuclidParams::movePerformRow(int delta)
{
    const int r = selectedPerformRow + delta;
    selectedPerformRow = static_cast<uint8_t>(r < 0 ? 0 : (r >= performRowCount() ? performRowCount() - 1 : r));
    refreshPerform();
}

void cEuclidParams::refreshPerform()
{
    if (view != View::perform)
        return;
    const bool frozen = euclidEngine.isFrozen();
    const bool jogging = euclidEngine.isJogging();
    lastPerformFrozen = frozen;
    lastPerformJog = jogging;
    lastPerformMorph = euclidEngine.getMorph();
    const uint8_t tab = performTab < toolCount ? performTab : 0;

    // The tools as tabs, the ones on marked.
    panelData.tabCount = toolCount;
    panelData.tabSelected = static_cast<int8_t>(tab);
    panelData.tabOn = 0;
    for (uint8_t i = 0; i < toolCount; i++)
    {
        panelData.tabs[i] = kToolNames[i];
        if (performToolOn(i))
            panelData.tabOn = static_cast<uint8_t>(panelData.tabOn | (1u << i));
    }

    panelData.mode = strEuclidPanelData::Mode::list;
    panelData.rows = performRowCount();
    if (selectedPerformRow >= panelData.rows)
        selectedPerformRow = static_cast<uint8_t>(panelData.rows - 1);
    for (uint8_t i = 0; i < strEuclidPanelData::kMaxRows; i++)
    {
        panelData.names[i] = nullptr;
        panelData.values[i] = nullptr;
    }
    const uint8_t header = kRowCounts[tab];
    for (uint8_t i = 0; i < header; i++)
    {
        panelData.names[i] = kRows[tab][i];
        panelData.values[i] = performText[i];
    }
    const size_t tn = sizeof(performText[0]);
    if (performToolOn(tab))
        snprintf(performText[0], tn, "On");
    else
        snprintf(performText[0], tn, "Off  (key %d)", tab + 1);
    uint32_t dim = 0;
    switch (tab)
    {
        case toolFreeze:
        {
            const int morph = euclidEngine.getMorph();
            snprintf(performText[1], tn, "%d.%d notes", morph / 1000, (morph % 1000) / 100);
            snprintf(performText[2], tn, "%s", euclidEngine.getMorphJump() ? "Jump" : "Glide");
            snprintf(performText[3], tn, "%d", euclidEngine.getMorphReach());
            snprintf(performText[4], tn, "+/-%d", euclidEngine.getBendRange());
            if (!frozen)
                dim |= 1ul << 1;
            if (euclidEngine.getMorphJump())
                dim |= 1ul << 4; // no bends
            break;
        }
        case toolArp:
        case toolScale:
            snprintf(performText[1], tn, "%s", (tab == toolArp ? performArpUpDown : performScaleUpDown) ? "Up-down" : "Up");
            break;
        case toolChord:
        {
            snprintf(performText[1], tn, "%s",
                     euclid::laneValueName(euclid::LaneKind::chord, static_cast<int8_t>(performChord)));
            const int inv = euclidEngine.getChordInversion();
            if (inv == 0)
                snprintf(performText[2], tn, "Root position");
            else
                snprintf(performText[2], tn, "%+d", inv);
            break;
        }
        case toolPulses: snprintf(performText[1], tn, "+%d", performPulses); break;
        case toolJog:
            snprintf(performText[1], tn, "%s a click", kJogNames[performJogStep % kJogSteps]);
            snprintf(performText[2], tn, "%s", performJogSnap ? "Back in time" : "Stay out of time");
            break;
        default: break;
    }

    if (hasChannelRows(tab))
    {
        for (uint8_t ch = 0; ch < euclid::kChannels; ch++)
        {
            const uint8_t row = static_cast<uint8_t>(header + ch);
            const char* name = euclidEngine.getNameShown(ch);
            if (name != nullptr)
                snprintf(performNames[ch], sizeof(performNames[ch]), "%d %.12s", ch + 1, name);
            else
                snprintf(performNames[ch], sizeof(performNames[ch]), "Channel %d", ch + 1);
            panelData.names[row] = performNames[ch];
            char* t = performText[row];
            const size_t n = sizeof(performText[row]);
            const euclid::Expression x = euclidEngine.getLiveExpression(ch);
            const bool playing = x.enabled && !x.muted;
            if (tab == toolFreeze && frozen && euclidEngine.isChannelFrozen(ch))
            {
                // The notes it's nearest, the ones after, and how far on.
                uint8_t steps = 0;
                const uint8_t at = euclidEngine.getMorphStep(ch, &steps);
                uint8_t now[8], next[8];
                char a[20], b[20];
                noteList(a, sizeof(a), now, euclidEngine.getMorphNotes(ch, at, now));
                if (steps == 0)
                    snprintf(t, n, "holds");
                else if (at < steps)
                {
                    noteList(b, sizeof(b), next, euclidEngine.getMorphNotes(ch, static_cast<uint8_t>(at + 1), next));
                    if (steps == 1)
                        snprintf(t, n, "%s  >  %s", a, b);
                    else
                        snprintf(t, n, "%s > %s  %d/%d", a, b, at, steps);
                }
                else
                    snprintf(t, n, "%s  %d/%d", a, at, steps);
            }
            else if (tab == toolJog && jogging && euclidEngine.isChannelJogged(ch))
                snprintf(t, n, "on the wheel");
            else if ((tab == toolFreeze && frozen) || (tab == toolJog && jogging))
            {
                snprintf(t, n, "%s", playing ? "plays on" : "off");
                dim |= 1ul << row;
            }
            else
            {
                // Whether the tool will take it (the wheel sets it).
                const uint8_t mask = tab == toolFreeze ? euclidEngine.getFreezeMask() : euclidEngine.getJogMask();
                const bool picked = (mask >> ch) & 1u;
                if (!playing)
                    snprintf(t, n, "off");
                else
                    snprintf(t, n, "%s", picked ? kToolNames[tab] : "Plays on");
                if (!picked || !playing)
                    dim |= 1ul << row;
            }
            panelData.values[row] = t;
        }
    }
    panelData.dimRows = dim;
    panelData.stagedRows = 0;
    panelData.meterCount = 0;
    refreshFrame();
}
