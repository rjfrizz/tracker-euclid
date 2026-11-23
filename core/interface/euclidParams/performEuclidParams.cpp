// The Perform page (the Performance key): live moves across all channels,
// played on the 8 keys under the screen. Freeze (key 1) is held: pressing it
// holds the notes of the channels picked (and the clock, if that's all of
// them), the wheel then moves each held note up or down the master key's
// tonic triad, a chord tone a click, gliding there over the Glide time (a
// single MIDI note bends, chords move a voice at a time), and letting go
// leaves the notes ringing until each channel's next one. The channel rows
// pick which channels it holds (all, to start with): the others play on.

#include "euclidParams/euclidParams.h"

#include "euclid/euclidEngine.h"
#include "euclid/euclidPlatform.h"
#include "mtStructs.h"

#include <stdio.h>
#include <string.h>

namespace
{
constexpr uint8_t kPerformHeader = 3; // Morph, Glide, Bend range
const char* const kPerformNames[kPerformHeader] = { "Morph", "Glide", "Bend range" };

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
    // While frozen the wheel morphs, a chord tone a click; otherwise it
    // sets the selected row.
    if (euclidEngine.isFrozen())
        euclidEngine.setMorph(euclidEngine.getMorph() + (delta > 0 ? 1 : -1));
    else if (selectedPerformRow == 1)
    {
        // 10 ms a click up to 200, then 50.
        const int g = euclidEngine.getGlideTime();
        const int step = (delta > 0 ? g >= 200 : g > 200) ? 50 : 10;
        const int v = g + (delta > 0 ? step : -step);
        euclidEngine.setGlideTime(static_cast<uint16_t>(v < 0 ? 0 : (v > 2000 ? 2000 : v)));
    }
    else if (selectedPerformRow == 2)
    {
        const int r = euclidEngine.getBendRange() + (delta > 0 ? 1 : -1);
        euclidEngine.setBendRange(static_cast<uint8_t>(r < 1 ? 1 : (r > 24 ? 24 : r)));
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
    const int tones = euclidEngine.getMorph();
    if (tones == 0)
        snprintf(performText[0], sizeof(performText[0]), "as held");
    else
        snprintf(performText[0], sizeof(performText[0]), "%+d chord tone%s", tones, tones == 1 || tones == -1 ? "" : "s");
    snprintf(performText[1], sizeof(performText[1]), "%d ms", euclidEngine.getGlideTime());
    snprintf(performText[2], sizeof(performText[2]), "+/-%d", euclidEngine.getBendRange());
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
        // Frozen: the notes it held, and the ones it's moving to.
        uint8_t held[8], to[8];
        const uint8_t heldCount = euclidEngine.getHeldNotes(ch, held);
        const uint8_t toCount = euclidEngine.getMorphNotes(ch, to);
        if (frozen && !euclidEngine.isChannelFrozen(ch))
        {
            snprintf(t, n, "plays on");
            dim |= 1ul << row;
        }
        else if (frozen && heldCount > 0)
        {
            char a[20], b[20];
            noteList(a, sizeof(a), held, heldCount);
            noteList(b, sizeof(b), to, toCount);
            if (euclidEngine.getMorph() == 0)
                snprintf(t, n, "%s", a);
            else
                snprintf(t, n, "%s > %s", a, b);
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
