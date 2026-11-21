// The Chain page (Song): an arrangement for the live engine, drawn like the
// stock Song editor with a block a step. Across the top: the chain's On and
// At-the-end switches; then a row a step, its scene ("Own settings" for the
// set's own), its set ("-" stays on the set playing), its bars and how many
// times it repeats. Left and right pick the part of a step to change.

#include "euclidParams/euclidParams.h"

#include "euclid/euclidChain.h"
#include "euclid/euclidPlatform.h"
#include "euclid/euclidSets.h"

#include <stdio.h>

namespace
{
constexpr uint8_t kChainHeader = 2; // On, At the end
constexpr uint8_t kChainFields = 4; // set, scene, bars, repeats

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
        switch (chainField)
        {
            case 0: s.set = static_cast<uint8_t>(clampInt(s.set + step, 0, euclid::kSetSlots)); break;
            case 1: s.scene = static_cast<uint8_t>(clampInt(s.scene + step, 0, 4)); break;
            case 2: s.bars = static_cast<uint8_t>(clampInt(s.bars + delta, 1, euclid::kChainMaxBars)); break;
            default: s.repeats = static_cast<uint8_t>(clampInt(s.repeats + step, 1, euclid::kChainMaxRepeats)); break;
        }
    }
    euclidChain::changed();
    refreshChain();
}

void cEuclidParams::moveChainRow(int delta)
{
    selectedChainRow = static_cast<uint8_t>(clampInt(selectedChainRow + delta, 0, chainRowCount() - 1));
    refreshChain();
}

// Left and right: across a step's parts, or between the two switches.
void cEuclidParams::moveChainField(int delta)
{
    if (selectedChainRow < kChainHeader)
        selectedChainRow = static_cast<uint8_t>(clampInt(selectedChainRow + delta, 0, kChainHeader - 1));
    else
        chainField = static_cast<uint8_t>(clampInt(chainField + delta, 0, kChainFields - 1));
    refreshChain();
}

// Insert: a copy of the step under the cursor after it (or after the last).
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

    panelData.mode = strEuclidPanelData::Mode::blocks;
    panelData.chainOn = c.on;
    panelData.chainLoop = c.loop;
    panelData.blockCount = c.count;
    for (uint8_t i = 0; i < c.count && i < strEuclidPanelData::kMaxBlocks; i++)
    {
        const euclid::ChainStep& s = c.steps[i];
        strEuclidPanelData::BlockData& b = panelData.blocks[i];
        b.set = s.set;
        b.scene = s.scene;
        b.bars = s.bars;
        b.repeats = s.repeats;
        b.playing = i == playing;
        b.repeat = b.playing ? static_cast<uint8_t>(barInStep / s.bars) : 0;
        b.bar = b.playing ? static_cast<uint8_t>(barInStep % s.bars) : 0;
    }
    panelData.blockSelected =
      selectedChainRow < kChainHeader ? static_cast<int8_t>(-1 - selectedChainRow)
                                      : static_cast<int8_t>(selectedChainRow - kChainHeader);
    panelData.blockField = chainField;
    display.refreshControl(panel);
}
