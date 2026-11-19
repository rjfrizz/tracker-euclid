// The Shift page (Fx 2): the selected channel's hand-drawn transpose lane,
// shown as a grid of up to 32 steps; pressing Fx 2 again shows its settings.
// See euclid::Shift.

#include "euclidParams/euclidParams.h"

#include <stdio.h>

namespace
{
constexpr uint8_t kPads = 48;

const char* const kShiftRowNames[cEuclidParams::shiftRowCount] = { "Shift", "Length", "Advance", "Units" };

const char* const kShiftHelp[] = {
    "Encoder: shift the step",
    "Delete: zero, Shift + Delete: all",
    "Pads pick a step",
    "Fx 2 again: settings",
};

int clampInt(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}
} // namespace

cEuclidParams::PadMap cEuclidParams::shiftPadMap(uint8_t row) const
{
    switch (row)
    {
        case shiftRowOn: return { PadMap::list, 2, 0, 0 };
        case shiftRowLength: return { PadMap::list, euclid::kShiftSteps, 1, 0 };
        case shiftRowAdvance: return { PadMap::list, static_cast<uint8_t>(euclid::ShiftAdvance::count), 0, 0 };
        case shiftRowUnits: return { PadMap::list, static_cast<uint8_t>(euclid::ShiftUnits::count), 0, 0 };
        default: return { PadMap::none, 0, 0, 0 };
    }
}

int cEuclidParams::shiftRowValue(uint8_t row, bool live) const
{
    const euclid::Expression x =
      live ? euclidEngine.getLiveExpression(selectedChannel) : euclidEngine.getExpression(selectedChannel);
    const euclid::Shift& s = x.shift;
    switch (row)
    {
        case shiftRowOn: return s.on ? 1 : 0;
        case shiftRowLength: return s.length;
        case shiftRowAdvance: return static_cast<int>(s.advance);
        case shiftRowUnits: return static_cast<int>(s.units);
        default: return kNoValue;
    }
}

void cEuclidParams::setShiftRowValue(uint8_t row, int v)
{
    euclid::Expression x = euclidEngine.getExpression(selectedChannel);
    euclid::Shift& s = x.shift;
    switch (row)
    {
        case shiftRowOn: s.on = v != 0; break;
        case shiftRowLength: s.length = static_cast<uint8_t>(clampInt(v, 1, euclid::kShiftSteps)); break;
        case shiftRowAdvance:
            s.advance = static_cast<euclid::ShiftAdvance>(
              clampInt(v, 0, static_cast<int>(euclid::ShiftAdvance::count) - 1));
            break;
        case shiftRowUnits:
            s.units =
              static_cast<euclid::ShiftUnits>(clampInt(v, 0, static_cast<int>(euclid::ShiftUnits::count) - 1));
            break;
        default: return;
    }
    euclidEngine.setExpression(selectedChannel, x);
    refreshShift();
    refreshTitle();
    refreshChannels();
    refreshRing();
    refreshPads();
}

// The encoder: a settings row in the settings list, otherwise the value of
// the step under the cursor. Drawing a step switches Shift on.
void cEuclidParams::changeShift(int16_t delta)
{
    if (shiftSettings)
    {
        const int step = delta > 0 ? 1 : -1;
        setShiftRowValue(selectedShiftRow, shiftRowValue(selectedShiftRow, false) + step);
        return;
    }

    euclid::Expression x = euclidEngine.getExpression(selectedChannel);
    euclid::Shift& s = x.shift;
    if (shiftCursor >= s.length)
        return;
    s.step[shiftCursor] =
      static_cast<int8_t>(clampInt(s.step[shiftCursor] + delta, -euclid::kMaxShift, euclid::kMaxShift));
    s.on = true;
    euclidEngine.setExpression(selectedChannel, x);
    refreshShift();
    refreshTitle();
    refreshChannels();
    refreshRing();
    refreshPads();
}

void cEuclidParams::moveShiftCursor(int8_t delta)
{
    const euclid::Shift& s = euclidEngine.getExpression(selectedChannel).shift;
    shiftCursor = static_cast<uint8_t>(clampInt(shiftCursor + delta, 0, s.length - 1));
    refreshShift();
    refreshPads();
}

void cEuclidParams::clearShiftStep(bool all)
{
    euclid::Expression x = euclidEngine.getExpression(selectedChannel);
    euclid::Shift& s = x.shift;
    if (all)
    {
        for (int8_t& v : s.step)
            v = 0;
    }
    else if (shiftCursor < euclid::kShiftSteps)
    {
        s.step[shiftCursor] = 0;
    }
    euclidEngine.setExpression(selectedChannel, x);
    refreshShift();
    refreshTitle();
    refreshChannels();
    refreshPads();
}

void cEuclidParams::refreshShift()
{
    if (view != View::shift)
        return;

    const euclid::Expression x = euclidEngine.getExpression(selectedChannel);
    const euclid::Shift& s = x.shift;

    if (shiftSettings)
    {
        panelData.mode = strEuclidPanelData::Mode::list;
        panelData.rows = shiftRowCount;
        for (uint8_t i = 0; i < strEuclidPanelData::kMaxRows; i++)
        {
            panelData.names[i] = i < shiftRowCount ? kShiftRowNames[i] : nullptr;
            panelData.values[i] = i < shiftRowCount ? shiftText[i] : nullptr;
        }
        snprintf(shiftText[shiftRowOn], sizeof(shiftText[0]), "%s", s.on ? "On" : "Off");
        snprintf(shiftText[shiftRowLength], sizeof(shiftText[0]), "%d steps", s.length);
        snprintf(shiftText[shiftRowAdvance], sizeof(shiftText[0]), "%s",
                 s.advance == euclid::ShiftAdvance::perStep ? "Per step" : "Per pulse");
        snprintf(shiftText[shiftRowUnits], sizeof(shiftText[0]), "%s",
                 s.units == euclid::ShiftUnits::semitones ? "Semitones" : "Scale steps");
        panelData.meterCount = 0;
        panelData.dimRows = s.on ? 0 : ((1ul << shiftRowLength) | (1ul << shiftRowAdvance) | (1ul << shiftRowUnits));
        panelData.stagedRows = stagedRowMask();
        refreshFrame();
        return;
    }

    // The steps: one cell per step, steps past the length shown as dashes.
    panelData.mode = strEuclidPanelData::Mode::grid;
    panelData.columns = 8;
    panelData.cells = euclid::kShiftSteps;
    panelData.usedCells = s.length;
    for (uint8_t i = 0; i < strEuclidPanelData::kMaxHelpLines; i++)
        panelData.help[i] = kShiftHelp[i];

    uint32_t dim = 0;
    for (uint8_t i = 0; i < euclid::kShiftSteps; i++)
    {
        if (s.step[i] == 0)
            snprintf(shiftCellText[i], sizeof(shiftCellText[i]), "0");
        else
            snprintf(shiftCellText[i], sizeof(shiftCellText[i]), "%+d", s.step[i]);
        panelData.cellText[i] = i < s.length ? shiftCellText[i] : nullptr;
        if (s.step[i] == 0 || !s.on)
            dim |= 1ul << i;
    }
    panelData.dimCells = dim;
    if (shiftCursor >= s.length)
        shiftCursor = static_cast<uint8_t>(s.length - 1);

    const int8_t played = euclidEngine.isRunning() ? euclidEngine.getShiftStep(selectedChannel) : -1;
    panelData.playingCell = s.on ? played : -1;
    lastShiftPlayed = played;

    if (!s.on)
        snprintf(noteHeaderText, sizeof(noteHeaderText), "Off: turn a step to start");
    else
        snprintf(noteHeaderText, sizeof(noteHeaderText), "%d steps, %s, %s", s.length,
                 s.advance == euclid::ShiftAdvance::perStep ? "per step" : "per pulse",
                 s.units == euclid::ShiftUnits::semitones ? "semis" : "scale");
    panelData.header = noteHeaderText;
    refreshFrame();
}
