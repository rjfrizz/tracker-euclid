// The Perform page (the Performance key): live moves across all channels.
// Freeze holds every note and the clock; the wheel then morphs each channel
// from the notes it held to its next ones (a single MIDI note glides with
// pitch bend, chords move a voice at a time); unfreezing carries on from
// whichever it's left nearer. A pad switches freeze on and off.

#include "euclidParams/euclidParams.h"

#include "euclid/euclidEngine.h"
#include "euclid/euclidPlatform.h"
#include "mtStructs.h"

#include <stdio.h>
#include <string.h>

namespace
{
constexpr uint8_t kPerformHeader = 3; // Freeze, Morph, Bend range
const char* const kPerformNames[kPerformHeader] = { "Freeze", "Morph", "Bend range" };

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

void cEuclidParams::togglePerformFreeze()
{
    if (euclidEngine.isFrozen())
    {
        euclidEngine.unfreeze();
        showMessage(euclidEngine.isRunning() ? "Playing on" : "Unfrozen");
    }
    else if (!euclidEngine.isRunning())
    {
        showMessage("Press Play first: freeze holds what plays");
        return;
    }
    else
    {
        euclidFreeze();
        selectedPerformRow = 1; // the wheel morphs straight away
        showMessage("Frozen: turn to morph");
    }
    refreshPerform();
    refreshTitle();
}

void cEuclidParams::changePerform(int16_t delta)
{
    const bool fine = buttons->isButtonPressed(interfaceButtonShift);
    switch (selectedPerformRow)
    {
        case 0:
            if ((delta > 0) != euclidEngine.isFrozen())
                togglePerformFreeze();
            return;
        case 2:
        {
            const int r = euclidEngine.getBendRange() + (delta > 0 ? 1 : -1);
            euclidEngine.setBendRange(static_cast<uint8_t>(r < 1 ? 1 : (r > 24 ? 24 : r)));
            break;
        }
        default:
            // Morph, from any channel row too.
            if (!euclidEngine.isFrozen())
            {
                showMessage("Freeze first (a pad)");
                return;
            }
            euclidEngine.setMorph(euclidEngine.getMorph() + delta * (fine ? 5 : 25));
            break;
    }
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
    snprintf(performText[0], sizeof(performText[0]), "%s", frozen ? "On" : "Off");
    snprintf(performText[1], sizeof(performText[1]), "%d%%", euclidEngine.getMorph() / 10);
    snprintf(performText[2], sizeof(performText[2]), "+/-%d", euclidEngine.getBendRange());
    for (uint8_t i = 0; i < kPerformHeader; i++)
    {
        panelData.names[i] = kPerformNames[i];
        panelData.values[i] = performText[i];
    }
    uint32_t dim = frozen ? 0 : (1ul << 1);
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
        uint8_t held[8], next[8];
        const uint8_t heldCount = euclidEngine.getHeldNotes(ch, held);
        const uint8_t nextCount = euclidEngine.getNextNotesFor(ch, next);
        if (frozen && nextCount > 0)
        {
            char a[28], b[28];
            noteList(a, sizeof(a), held, heldCount);
            noteList(b, sizeof(b), next, nextCount);
            snprintf(t, n, "%s  >  %s", a, b);
        }
        else if (frozen)
            snprintf(t, n, "holds (no next notes)");
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
