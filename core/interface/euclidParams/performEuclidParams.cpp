// The Perform page (the Performance key): live moves across all channels,
// played on the 8 keys under the screen. Freeze (key 1) is held: pressing it
// holds every note and the clock, the wheel then walks each channel through
// its coming notes, the whole sequence over the wheel's travel (a single
// MIDI note glides with pitch bend, chords move a voice at a time), and
// letting go carries on from the notes it's left nearest.

#include "euclidParams/euclidParams.h"

#include "euclid/euclidEngine.h"
#include "euclid/euclidPlatform.h"
#include "mtStructs.h"

#include <stdio.h>
#include <string.h>

namespace
{
constexpr uint8_t kPerformHeader = 2; // Morph, Bend range
const char* const kPerformNames[kPerformHeader] = { "Morph", "Bend range" };

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

// The keys under the screen: key 1 is Freeze, held.
void cEuclidParams::performKey(uint8_t key, bool pressed)
{
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
    {
        const int r = euclidEngine.getBendRange() + (delta > 0 ? 1 : -1);
        euclidEngine.setBendRange(static_cast<uint8_t>(r < 1 ? 1 : (r > 24 ? 24 : r)));
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
    snprintf(performText[0], sizeof(performText[0]), "%d%%", euclidEngine.getMorph() / 10);
    snprintf(performText[1], sizeof(performText[1]), "+/-%d", euclidEngine.getBendRange());
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
        // Frozen: the notes it's nearest, the ones after, and how far along.
        uint8_t steps = 0;
        const uint8_t at = euclidEngine.getMorphStep(ch, &steps);
        if (frozen && steps > 0)
        {
            uint8_t now[8], next[8];
            const uint8_t nowCount = euclidEngine.getMorphNotes(ch, at, now);
            char a[20], b[20];
            noteList(a, sizeof(a), now, nowCount);
            if (at < steps)
            {
                const uint8_t nextCount = euclidEngine.getMorphNotes(ch, static_cast<uint8_t>(at + 1), next);
                noteList(b, sizeof(b), next, nextCount);
                snprintf(t, n, "%s > %s  %d/%d", a, b, at, steps);
            }
            else
                snprintf(t, n, "%s  %d/%d", a, at, steps);
        }
        else if (frozen)
            snprintf(t, n, "holds");
        else
        {
            const euclid::Expression x = euclidEngine.getLiveExpression(ch);
            snprintf(t, n, "%s", x.enabled && !x.muted ? "" : "off");
            dim |= 1ul << row;
        }
        panelData.values[row] = t;
    }
    panelData.dimRows = dim;
    panelData.stagedRows = 0;
    panelData.meterCount = 0;
    refreshFrame();
}
