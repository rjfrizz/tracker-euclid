// The Chain page (Song): an arrangement for the live engine. Two rows turn
// it on and choose what happens at the end; then one row a step, each a set
// ("-" stays on the set playing), a scene ("-" the set's own settings) and
// a length in bars. Left and right pick the part of a step to change.

#include "euclidParams/euclidParams.h"

#include "euclid/euclidChain.h"
#include "euclid/euclidSets.h"
#include "euclid/euclidPlatform.h"

#include <stdio.h>

namespace
{
constexpr uint8_t kChainHeader = 2; // On, At end

int clampInt(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}
} // namespace

uint8_t cEuclidParams::chainRowCount() const
{
    return static_cast<uint8_t>(kChainHeader + euclidChain::get().count);
}

void cEuclidParams::changeChain(int16_t delta)
{
    euclid::Chain& c = euclidChain::get();
    const int step = delta > 0 ? 1 : -1;
    const uint8_t row = selectedChainRow;
    if (row == 0)
        c.on = step > 0;
    else if (row == 1)
        c.loop = step < 0; // Loop, then Stop
    else
    {
        euclid::ChainStep& s = c.steps[row - kChainHeader];
        if (chainField == 0)
            s.set = static_cast<uint8_t>(clampInt(s.set + step, 0, euclid::kSetSlots));
        else if (chainField == 1)
            s.scene = static_cast<uint8_t>(clampInt(s.scene + step, 0, 4));
        else
            s.bars = static_cast<uint8_t>(clampInt(s.bars + delta, 1, euclid::kChainMaxBars));
    }
    euclidChain::changed();
    refreshChain();
}

void cEuclidParams::moveChainRow(int delta)
{
    selectedChainRow = static_cast<uint8_t>(clampInt(selectedChainRow + delta, 0, chainRowCount() - 1));
    refreshChain();
}

void cEuclidParams::moveChainField(int delta)
{
    chainField = static_cast<uint8_t>(clampInt(chainField + delta, 0, 2));
    refreshChain();
}

// Insert: a copy of the step under the cursor after it (or a first step).
void cEuclidParams::insertChainStep()
{
    euclid::Chain& c = euclidChain::get();
    if (c.count >= euclid::kChainSteps)
    {
        showMessage("The chain is full");
        return;
    }
    const int at = selectedChainRow >= kChainHeader ? selectedChainRow - kChainHeader : c.count - 1;
    for (int i = c.count; i > at + 1; i--)
        c.steps[i] = c.steps[i - 1];
    c.steps[at + 1] = c.steps[at];
    c.count++;
    selectedChainRow = static_cast<uint8_t>(kChainHeader + at + 1);
    euclidChain::changed();
    refreshChain();
}

void cEuclidParams::deleteChainStep()
{
    euclid::Chain& c = euclidChain::get();
    if (selectedChainRow < kChainHeader || c.count <= 1)
        return;
    const int at = selectedChainRow - kChainHeader;
    for (int i = at; i + 1 < c.count; i++)
        c.steps[i] = c.steps[i + 1];
    c.steps[c.count - 1] = euclid::ChainStep();
    c.count--;
    if (selectedChainRow >= chainRowCount())
        selectedChainRow = static_cast<uint8_t>(chainRowCount() - 1);
    euclidChain::changed();
    refreshChain();
}

void cEuclidParams::refreshChain()
{
    if (view != View::chain)
        return;
    const euclid::Chain& c = euclidChain::get();
    uint32_t barInStep = 0, stepBars = 0;
    const int8_t playing = euclidChain::playingStep(&barInStep, &stepBars);
    lastChainPlaying = playing;
    lastChainBar = barInStep;

    panelData.rows = chainRowCount();
    for (uint8_t i = 0; i < strEuclidPanelData::kMaxRows; i++)
    {
        panelData.names[i] = nullptr;
        panelData.values[i] = nullptr;
    }
    panelData.names[0] = "Chain";
    snprintf(chainText[0], sizeof(chainText[0]), "%s", c.on ? "On: Play runs it" : "Off");
    panelData.names[1] = "At the end";
    snprintf(chainText[1], sizeof(chainText[1]), "%s", c.loop ? "Loop" : "Stop");

    // Steps: "3", "[Set 2]  B  8 bars", the part being changed in brackets;
    // the one playing shows its bar.
    uint32_t staged = 0;
    for (uint8_t i = 0; i < c.count && kChainHeader + i < strEuclidPanelData::kMaxRows; i++)
    {
        const euclid::ChainStep& s = c.steps[i];
        const uint8_t row = static_cast<uint8_t>(kChainHeader + i);
        const bool here = row == selectedChainRow;
        char set[8], scene[4], bars[16];
        if (s.set == 0)
            snprintf(set, sizeof(set), "Set -");
        else
            snprintf(set, sizeof(set), "Set %d", s.set);
        snprintf(scene, sizeof(scene), "%c", s.scene == 0 ? '-' : static_cast<char>('A' + s.scene - 1));
        if (i == playing)
            snprintf(bars, sizeof(bars), "%lu/%d bars", static_cast<unsigned long>(barInStep + 1), s.bars);
        else
            snprintf(bars, sizeof(bars), "%d bar%s", s.bars, s.bars == 1 ? "" : "s");
        snprintf(chainNames[i], sizeof(chainNames[i]), "%d", i + 1);
        snprintf(chainText[row], sizeof(chainText[row]), here && chainField == 0 ? "[%s] %s %s"
                                                         : here && chainField == 1 ? "%s [%s] %s"
                                                         : here ? "%s %s [%s]"
                                                                : "%s  %s  %s",
                 set, scene, bars);
        panelData.names[row] = chainNames[i];
        if (i == playing)
            staged |= 1ul << row; // the step playing shows in orange
    }
    for (uint8_t row = 0; row < panelData.rows && row < strEuclidPanelData::kMaxRows; row++)
        panelData.values[row] = chainText[row];
    panelData.dimRows = c.on ? 0 : ~1ul; // all but the On row while it's off
    panelData.stagedRows = staged;
    panelData.meterCount = 0;
    refreshFrame();
}
