// The Perform page (the Performance key): live moves across all channels.
// Seven tools, a key each under the screen and a tab each across the top;
// a key turns its tool on, again off (hands free), and shows its tab, where
// its settings are, ending with a row a channel for which channels it takes
// (all to start with; the Performance key also moves on a tab).
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
//   Progression: the master key moves through a progression's chords, each
//     on its beat, round and round (off: back to the key it started in).
//     Built-in ones, and four of your own to edit (editing a built-in one
//     copies it to the first empty one of yours); the pads jump to a chord.

#include "euclidParams/euclidParams.h"

#include "euclid/euclidEngine.h"
#include "euclid/euclidPlatform.h"
#include "scales.h"
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
    toolProg,
    toolCount
};
const char* const kToolNames[toolCount] = { "Freeze", "Walk", "Arp", "Scale", "Chord", "Pulses", "Prog" };

// Each tab's rows before its channel rows.
const char* const kFreezeRows[] = { "Freeze", "Morph", "Move", "Notes ahead", "Bend range" };
const char* const kWalkRows[] = { "Walk" };
const char* const kArpRows[] = { "Arp", "Direction" };
const char* const kScaleRows[] = { "Scale", "Direction" };
const char* const kChordRows[] = { "Chord", "Type", "Inversion" };
const char* const kPulsesRows[] = { "Pulses", "Add" };
const char* const kProgRows[] = { "Progression", "Pick", "Chords", "Chord", "Root", "Type", "Beats", "Chords used" };
const char* const* const kRows[toolCount] = { kFreezeRows, kWalkRows, kArpRows, kScaleRows,
                                              kChordRows,  kPulsesRows, kProgRows };
const uint8_t kRowCounts[toolCount] = { 5, 1, 2, 2, 3, 2, 8 };
// Every tool's tab ends with a row a channel: whether it takes that one.
bool hasChannelRows(uint8_t tool)
{
    return tool < toolCount && tool != toolProg; // the progression moves the master key
}
// The row a tab opens on: what's played live.
const uint8_t kFirstRow[toolCount] = { 1, 0, 1, 1, 2, 1, 1 };

// A chord's root as a degree of the key the progression started in.
const char* const kDegrees[12] = { "I", "bII", "II", "bIII", "III", "IV", "#IV", "V", "bVI", "VI", "bVII", "VII" };
const char* const kNoteNames[12] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };

// Presets first, then the user ones.
uint8_t progCount()
{
    return static_cast<uint8_t>(euclid::progressionPresetCount() + euclid::kUserProgs);
}
const euclid::Progression& progAt(uint8_t i)
{
    const uint8_t presets = euclid::progressionPresetCount();
    return i < presets ? euclid::progressionPreset(i) : euclidProgressions::user(static_cast<uint8_t>(i - presets));
}
bool isUserProg(uint8_t i)
{
    return i >= euclid::progressionPresetCount();
}
// The key the chords are named in: the progression's home while it plays.
uint8_t progHomeRoot()
{
    return euclidEngine.isProgressing() ? euclidEngine.getProgressionHome().root
                                        : euclidEngine.getLiveGlobalKey().root;
}


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
    if (tool == toolProg)
        return euclidEngine.isProgressing();
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
    const uint8_t modeTool = (performOn & (1u << toolArp)) ? toolArp : ((performOn & (1u << toolScale)) ? toolScale : toolWalk);
    euclidEngine.setOverrideMasks(performMask[modeTool], performMask[toolChord], performMask[toolPulses]);
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
    else if (tool == toolProg)
    {
        if (on)
        {
            const euclid::Progression& p = progAt(performProg);
            if (p.count == 0)
            {
                showMessage("Empty: pick another, or edit this one");
                return;
            }
            euclidEngine.startProgression(p);
            performProgSlot = 0;
        }
        else
            euclidEngine.stopProgression();
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

// The progression to edit: the user one picked, or a copy of the preset
// picked in the first empty user one (picked from then on).
bool cEuclidParams::editPerformProg()
{
    if (isUserProg(performProg))
        return true;
    for (uint8_t u = 0; u < euclid::kUserProgs; u++)
    {
        euclid::Progression& p = euclidProgressions::user(u);
        if (p.count != 0)
            continue;
        p = progAt(performProg);
        char msg[40];
        snprintf(msg, sizeof(msg), "Editing a copy: User %d", u + 1);
        showMessage(msg);
        performProg = static_cast<uint8_t>(euclid::progressionPresetCount() + u);
        euclidProgressions::changed();
        if (euclidEngine.isProgressing())
            euclidEngine.setProgression(p);
        return true;
    }
    showMessage("Your 4 are full: pick one of yours to edit");
    return false;
}

void cEuclidParams::changePerform(int16_t delta)
{
    const int dir = delta > 0 ? 1 : -1;
    const uint8_t row = selectedPerformRow;
    const bool shift = buttons->isButtonPressed(interfaceButtonShift);

    // Live: Freeze's wheel morphs.
    if (performTab == toolFreeze && euclidEngine.isFrozen())
    {
        const int click = euclidEngine.getMorphReach() * 1000 / kMorphClicks;
        euclidEngine.setMorph(euclidEngine.getMorph() + delta * (shift ? click / 5 : click));
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
            // Heard straight away if the tool's on.
            uint8_t& m = performMask[performTab];
            m = static_cast<uint8_t>(dir > 0 ? (m | bit) : (m & ~bit));
            applyPerformMode();
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
            case toolProg:
                if (row == 1)
                {
                    // Another progression (playing on, if it's on).
                    performProg = static_cast<uint8_t>(clampInt(performProg + dir, 0, progCount() - 1));
                    performProgSlot = 0;
                    if (euclidEngine.isProgressing() && progAt(performProg).count > 0)
                        euclidEngine.startProgression(progAt(performProg));
                }
                else if (row == 2)
                    showMessage("Pads jump to a chord while it plays");
                else if (row == 3)
                {
                    const euclid::Progression& p = progAt(performProg);
                    performProgSlot = static_cast<uint8_t>(clampInt(performProgSlot + dir, 0, p.count > 0 ? p.count - 1 : 0));
                }
                else if (editPerformProg())
                {
                    euclid::Progression& p = euclidProgressions::user(
                      static_cast<uint8_t>(performProg - euclid::progressionPresetCount()));
                    if (p.count == 0)
                    {
                        // An empty one starts with a chord on the key's root.
                        p.count = 1;
                        p.slot[0] = euclid::ProgSlot();
                        snprintf(p.name, sizeof(p.name), "User %d", performProg - euclid::progressionPresetCount() + 1);
                    }
                    if (performProgSlot >= p.count)
                        performProgSlot = static_cast<uint8_t>(p.count - 1);
                    euclid::ProgSlot& slot = p.slot[performProgSlot];
                    if (row == 4)
                        slot.root = static_cast<uint8_t>((slot.root + 12 + dir) % 12);
                    else if (row == 5)
                        slot.scale = static_cast<uint8_t>(clampInt(slot.scale + dir, 1, MAX_SCALE));
                    else if (row == 6)
                        slot.beats = static_cast<uint8_t>(clampInt(slot.beats + dir, 1, 16));
                    else if (row == 7)
                    {
                        // More chords: each new one a copy of the last.
                        const int count = clampInt(p.count + dir, 1, euclid::kProgSlots);
                        for (int k = p.count; k < count; k++)
                            p.slot[k] = p.slot[p.count - 1];
                        p.count = static_cast<uint8_t>(count);
                        if (performProgSlot >= p.count)
                            performProgSlot = static_cast<uint8_t>(p.count - 1);
                    }
                    euclidProgressions::changed();
                    if (euclidEngine.isProgressing())
                        euclidEngine.setProgression(p);
                }
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
    lastPerformFrozen = frozen;
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
        case toolProg:
        {
            const euclid::Progression& p = progAt(performProg);
            const uint8_t home = progHomeRoot();
            if (isUserProg(performProg))
                snprintf(performText[1], tn, "%s",
                         p.count > 0 ? p.name : "(empty: edit to start)");
            else
                snprintf(performText[1], tn, "%s", p.name);
            // The chords, the one playing in brackets.
            const bool playing = euclidEngine.isProgressing();
            const uint8_t at = euclidEngine.getProgressionSlot();
            size_t n = 0;
            performText[2][0] = 0;
            for (uint8_t k = 0; k < p.count && n + 10 < tn; k++)
            {
                char c[12];
                euclid::progressionChordName(p.slot[k], home, c, sizeof(c));
                const bool now = playing && k == at;
                n += static_cast<size_t>(snprintf(performText[2] + n, tn - n, "%s%s%s%s", k > 0 ? " " : "",
                                                  now ? "[" : "", c, now ? "]" : ""));
            }
            if (p.count == 0)
                snprintf(performText[2], tn, "-");
            if (performProgSlot >= p.count && p.count > 0)
                performProgSlot = static_cast<uint8_t>(p.count - 1);
            if (p.count > 0)
            {
                const euclid::ProgSlot& sl = p.slot[performProgSlot];
                char c[12];
                euclid::progressionChordName(sl, home, c, sizeof(c));
                snprintf(performText[3], tn, "%d of %d  (%s)", performProgSlot + 1, p.count, c);
                snprintf(performText[4], tn, "%s  (%s)", kDegrees[sl.root % 12], kNoteNames[(home + sl.root) % 12]);
                snprintf(performText[5], tn, "%s", sl.scale <= MAX_SCALE ? scaleNames[sl.scale] : "?");
                snprintf(performText[6], tn, "%d beat%s", sl.beats, sl.beats == 1 ? "" : "s");
                snprintf(performText[7], tn, "%d", p.count);
            }
            else
                for (uint8_t r = 3; r < 8; r++)
                    snprintf(performText[r], tn, "-");
            lastPerformProg = playing;
            lastPerformProgSlot = at;
            break;
        }
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
            else if (tab == toolFreeze && frozen)
            {
                snprintf(t, n, "%s", playing ? "plays on" : "off");
                dim |= 1ul << row;
            }
            else
            {
                // Whether the tool will take it (the wheel sets it).
                const uint8_t mask = tab == toolFreeze ? euclidEngine.getFreezeMask() : performMask[tab];
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
