// The Perform page (the Performance key): live moves across all channels,
// played on the 8 keys under the screen. Freeze (key 1) is held: pressing it
// holds the notes of the channels picked (and the clock, if that's all of
// them), the wheel then morphs each channel from the notes it held on
// through its next ones, as many as Notes ahead (a single MIDI note glides
// with pitch bend, chords move a voice at a time), and letting go carries on
// from the notes it's left nearest.
// Walk, Arp and Scale (keys 2-4), held, put every channel into that mode
// from its next note (Arp and Scale up, or up and down, as their rows say),
// each starting near the note it played last; letting go puts each back. The
// channel rows
// pick which channels it holds (all, to start with): the others play on.

#include "euclidParams/euclidParams.h"

#include "euclid/euclidEngine.h"
#include "euclid/euclidPlatform.h"
#include "mtStructs.h"

#include <stdio.h>
#include <string.h>

namespace
{
constexpr uint8_t kPerformHeader = 5; // Morph, Notes ahead, Bend range, Arp, Scale
const char* const kPerformNames[kPerformHeader] = { "Morph", "Notes ahead", "Bend range", "Arp", "Scale" };

void noteList(char* out, size_t length, const uint8_t* notes, uint8_t count)
{
    size_t n = 0;
    out[0] = 0;
    for (uint8_t i = 0; i < count && n + 5 < length; i++)
        n += static_cast<size_t>(snprintf(out + n, length - n, i == 0 ? "%s" : " %s", mtNotes[notes[i] & 127]));
    if (count == 0)
        snprintf(out, length, "-");
}
} // namespace

uint8_t cEuclidParams::performRowCount() const
{
    return static_cast<uint8_t>(kPerformHeader + euclid::kChannels);
}

// The mode the held Walk, Arp or Scale key puts every channel in (the last
// pressed of those held), or none.
void cEuclidParams::applyPerformMode()
{
    int8_t mode = -1;
    switch (performModeLast)
    {
        case 0: mode = static_cast<int8_t>(euclid::NoteMode::walk); break;
        case 1:
            mode = static_cast<int8_t>(performArpUpDown ? euclid::NoteMode::arpUpDown : euclid::NoteMode::arpUp);
            break;
        case 2:
            mode = static_cast<int8_t>(performScaleUpDown ? euclid::NoteMode::scaleUpDown : euclid::NoteMode::scaleUp);
            break;
        default: break;
    }
    euclidEngine.setModeOverride(mode);
}

// The keys under the screen: key 1 is Freeze, keys 2-4 Walk, Arp and Scale,
// all held.
void cEuclidParams::performKey(uint8_t key, bool pressed)
{
    if (key >= 1 && key <= 3)
    {
        const uint8_t i = static_cast<uint8_t>(key - 1);
        if (pressed)
        {
            performModeHeld = static_cast<uint8_t>(performModeHeld | (1u << i));
            performModeLast = static_cast<int8_t>(i);
            showMessage(i == 0 ? "All channels walk" : (i == 1 ? "All channels arp" : "All channels run the scale"));
        }
        else
        {
            performModeHeld = static_cast<uint8_t>(performModeHeld & ~(1u << i));
            if (performModeLast == static_cast<int8_t>(i))
            {
                // Another still held takes over.
                performModeLast = -1;
                for (int8_t k = 0; k < 3; k++)
                    if (performModeHeld & (1u << k))
                        performModeLast = k;
            }
        }
        applyPerformMode();
        refreshPerform();
        refreshChannels();
        return;
    }
    if (key != 0)
        return; // more to come
    if (pressed)
    {
        if (!euclidEngine.isRunning())
        {
            showMessage("Play first: freeze holds what plays");
            return;
        }
        euclidFreeze();
        if (!euclidEngine.isFrozen())
        {
            showMessage("No channel playing is set to freeze");
            return;
        }
        showMessage("Frozen: turn to morph");
    }
    else if (euclidEngine.isFrozen())
        euclidEngine.unfreeze();
    refreshPerform();
    refreshChannels();
    refreshTitle();
}

void cEuclidParams::changePerform(int16_t delta)
{
    // While frozen the wheel morphs; otherwise it sets the selected row.
    if (euclidEngine.isFrozen())
    {
        const bool fine = buttons->isButtonPressed(interfaceButtonShift);
        euclidEngine.setMorph(euclidEngine.getMorph() + delta * (fine ? 8 : 38));
    }
    else if (selectedPerformRow == 1)
        euclidEngine.setMorphReach(static_cast<uint8_t>(euclidEngine.getMorphReach() + (delta > 0 ? 1 : -1)));
    else if (selectedPerformRow == 2)
    {
        const int r = euclidEngine.getBendRange() + (delta > 0 ? 1 : -1);
        euclidEngine.setBendRange(static_cast<uint8_t>(r < 1 ? 1 : (r > 24 ? 24 : r)));
    }
    else if (selectedPerformRow == 3 || selectedPerformRow == 4)
    {
        // Up, or up and down (heard straight away if the key's held).
        bool& upDown = selectedPerformRow == 3 ? performArpUpDown : performScaleUpDown;
        upDown = delta > 0;
        applyPerformMode();
    }
    else if (selectedPerformRow >= kPerformHeader)
    {
        // A channel row: whether Freeze holds it.
        const uint8_t bit = static_cast<uint8_t>(1u << (selectedPerformRow - kPerformHeader));
        const uint8_t mask = euclidEngine.getFreezeMask();
        euclidEngine.setFreezeMask(static_cast<uint8_t>(delta > 0 ? (mask | bit) : (mask & ~bit)));
    }
    else
        showMessage("Hold Freeze (key 1) and turn");
    refreshPerform();
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

    panelData.mode = strEuclidPanelData::Mode::list;
    panelData.rows = performRowCount();
    for (uint8_t i = 0; i < strEuclidPanelData::kMaxRows; i++)
    {
        panelData.names[i] = nullptr;
        panelData.values[i] = nullptr;
    }
    // How far on, in notes (a tenth at a time).
    const int morph = euclidEngine.getMorph();
    snprintf(performText[0], sizeof(performText[0]), "%d.%d notes", morph / 1000, (morph % 1000) / 100);
    snprintf(performText[1], sizeof(performText[1]), "%d", euclidEngine.getMorphReach());
    snprintf(performText[2], sizeof(performText[2]), "+/-%d", euclidEngine.getBendRange());
    snprintf(performText[3], sizeof(performText[3]), "%s", performArpUpDown ? "Up-down" : "Up");
    snprintf(performText[4], sizeof(performText[4]), "%s", performScaleUpDown ? "Up-down" : "Up");
    for (uint8_t i = 0; i < kPerformHeader; i++)
    {
        panelData.names[i] = kPerformNames[i];
        panelData.values[i] = performText[i];
    }
    uint32_t dim = frozen ? 0 : 1ul; // Morph, only while frozen
    for (uint8_t ch = 0; ch < euclid::kChannels; ch++)
    {
        const uint8_t row = static_cast<uint8_t>(kPerformHeader + ch);
        const char* name = euclidEngine.getNameShown(ch);
        if (name != nullptr)
            snprintf(performNames[ch], sizeof(performNames[ch]), "%d %.12s", ch + 1, name);
        else
            snprintf(performNames[ch], sizeof(performNames[ch]), "Channel %d", ch + 1);
        panelData.names[row] = performNames[ch];
        char* t = performText[row];
        const size_t n = sizeof(performText[row]);
        // Frozen: the notes it's nearest, the ones after, and how far on.
        uint8_t steps = 0;
        const uint8_t at = euclidEngine.getMorphStep(ch, &steps);
        if (frozen && !euclidEngine.isChannelFrozen(ch))
        {
            snprintf(t, n, "plays on");
            dim |= 1ul << row;
        }
        else if (frozen && steps > 0)
        {
            uint8_t now[8], next[8];
            const uint8_t nowCount = euclidEngine.getMorphNotes(ch, at, now);
            char a[20], b[20];
            noteList(a, sizeof(a), now, nowCount);
            if (at < steps)
            {
                const uint8_t nextCount = euclidEngine.getMorphNotes(ch, static_cast<uint8_t>(at + 1), next);
                noteList(b, sizeof(b), next, nextCount);
                if (steps == 1)
                    snprintf(t, n, "%s  >  %s", a, b);
                else
                    snprintf(t, n, "%s > %s  %d/%d", a, b, at, steps);
            }
            else
                snprintf(t, n, "%s  %d/%d", a, at, steps);
        }
        else if (frozen)
            snprintf(t, n, "holds");
        else
        {
            // Not frozen: whether Freeze will hold it (the wheel sets it).
            const euclid::Expression x = euclidEngine.getLiveExpression(ch);
            const bool picked = (euclidEngine.getFreezeMask() >> ch) & 1u;
            if (!x.enabled || x.muted)
                snprintf(t, n, "off");
            else
                snprintf(t, n, "%s", picked ? "Freeze" : "Plays on");
            if (!picked || !x.enabled || x.muted)
                dim |= 1ul << row;
        }
        panelData.values[row] = t;
    }
    panelData.dimRows = dim;
    panelData.stagedRows = 0;
    panelData.meterCount = 0;
    refreshFrame();
}
