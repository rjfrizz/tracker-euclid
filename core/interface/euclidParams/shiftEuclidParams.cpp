// The Shift page (Fx 2): the selected channel's hand-drawn lanes, shown as a
// grid of up to 32 steps; pressing Fx 2 again shows the lane's settings,
// whose first row picks the lane. The pitch lane transposes (euclid::Shift);
// the others set the mode, division, note order or chord step by step, or
// leave a step at "-" for the channel's own (euclid::Lane).

#include "euclidParams/euclidParams.h"

#include <stdio.h>

namespace
{
constexpr uint8_t kPads = 48;
constexpr uint8_t kLanes = 1 + euclid::kLaneKinds;

const char* const kShiftRowNames[cEuclidParams::shiftRowCount] = { "Lane", "Shift", "Length", "Advance", "Units" };
const char* const kLaneNames[kLanes] = { "Pitch", "Mode", "Division", "Order", "Chord" };

const char* const kShiftHelp[] = {
    "Encoder: change the step, pads pick one",
    "Delete: clear, Shift + Delete: all",
    "Fx 2: next lane, +Shift: settings",
    nullptr,
};
// The tabs across the top: a lane each (short, to fit five).
const char* const kLaneTabs[] = { "Pitch", "Mode", "Div", "Order", "Chord" };

int clampInt(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

const char* advanceName(euclid::ShiftAdvance a, bool capital)
{
    switch (a)
    {
        case euclid::ShiftAdvance::perStep: return capital ? "Per step" : "per step";
        case euclid::ShiftAdvance::perPulse: return capital ? "Per pulse" : "per pulse";
        default: return capital ? "Per cycle" : "per cycle";
    }
}

euclid::LaneKind kindOf(uint8_t lane)
{
    return static_cast<euclid::LaneKind>(lane - 1);
}

bool isDivision(uint8_t lane)
{
    return lane != 0 && kindOf(lane) == euclid::LaneKind::division;
}
} // namespace

bool cEuclidParams::shiftLaneOn() const
{
    const euclid::Expression x = euclidEngine.getExpression(selectedChannel);
    return shiftLane == 0 ? x.shift.on : x.lanes[shiftLane - 1].on;
}

uint8_t cEuclidParams::shiftLaneLength() const
{
    const euclid::Expression x = euclidEngine.getExpression(selectedChannel);
    return shiftLane == 0 ? x.shift.length : x.lanes[shiftLane - 1].length;
}

bool cEuclidParams::shiftStepUsed(uint8_t step) const
{
    if (step >= euclid::kShiftSteps)
        return false;
    const euclid::Expression x = euclidEngine.getExpression(selectedChannel);
    return shiftLane == 0 ? x.shift.step[step] != 0 : x.lanes[shiftLane - 1].step[step] != euclid::kLaneNone;
}

int8_t cEuclidParams::shiftPlayedStep() const
{
    if (!euclidEngine.isRunning())
        return -1;
    return shiftLane == 0 ? euclidEngine.getShiftStep(selectedChannel)
                          : euclidEngine.getLaneStep(selectedChannel, kindOf(shiftLane));
}

cEuclidParams::PadMap cEuclidParams::shiftPadMap(uint8_t row) const
{
    switch (row)
    {
        case shiftRowLane: return { PadMap::list, kLanes, 0, 0 };
        case shiftRowOn: return { PadMap::list, 2, 0, 0 };
        case shiftRowLength: return { PadMap::list, euclid::kShiftSteps, 1, 0 };
        case shiftRowAdvance:
            return isDivision(shiftLane) ? PadMap{ PadMap::none, 0, 0, 0 }
                                         : PadMap{ PadMap::list, static_cast<uint8_t>(euclid::ShiftAdvance::count), 0, 0 };
        case shiftRowUnits: return { PadMap::list, static_cast<uint8_t>(euclid::ShiftUnits::count), 0, 0 };
        default: return { PadMap::none, 0, 0, 0 };
    }
}

int cEuclidParams::shiftRowValue(uint8_t row, bool live) const
{
    if (row == shiftRowLane)
        return shiftLane;
    const euclid::Expression x =
      live ? euclidEngine.getLiveExpression(selectedChannel) : euclidEngine.getExpression(selectedChannel);
    if (shiftLane != 0)
    {
        const euclid::Lane& l = x.lanes[shiftLane - 1];
        switch (row)
        {
            case shiftRowOn: return l.on ? 1 : 0;
            case shiftRowLength: return l.length;
            case shiftRowAdvance: return static_cast<int>(l.advance);
            default: return kNoValue;
        }
    }
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
    if (row == shiftRowLane)
    {
        // Just which lane the page shows.
        shiftLane = static_cast<uint8_t>(clampInt(v, 0, kLanes - 1));
        shiftCursor = 0;
        refreshShift();
        refreshTitle();
        refreshRing();
        refreshPads();
        return;
    }

    euclid::Expression x = euclidEngine.getExpression(selectedChannel);
    const int advanceMax = static_cast<int>(euclid::ShiftAdvance::count) - 1;
    if (shiftLane != 0)
    {
        euclid::Lane& l = x.lanes[shiftLane - 1];
        switch (row)
        {
            case shiftRowOn: l.on = v != 0; break;
            case shiftRowLength: l.length = static_cast<uint8_t>(clampInt(v, 1, euclid::kShiftSteps)); break;
            case shiftRowAdvance:
                if (isDivision(shiftLane))
                    return; // always per cycle
                l.advance = static_cast<euclid::ShiftAdvance>(clampInt(v, 0, advanceMax));
                break;
            default: return;
        }
    }
    else
    {
        euclid::Shift& s = x.shift;
        switch (row)
        {
            case shiftRowOn: s.on = v != 0; break;
            case shiftRowLength: s.length = static_cast<uint8_t>(clampInt(v, 1, euclid::kShiftSteps)); break;
            case shiftRowAdvance: s.advance = static_cast<euclid::ShiftAdvance>(clampInt(v, 0, advanceMax)); break;
            case shiftRowUnits:
                s.units =
                  static_cast<euclid::ShiftUnits>(clampInt(v, 0, static_cast<int>(euclid::ShiftUnits::count) - 1));
                break;
            default: return;
        }
    }
    euclidEngine.setExpression(selectedChannel, x);
    refreshShift();
    refreshTitle();
    refreshChannels();
    refreshRing();
    refreshPads();
}

// The encoder: a settings row in the settings list, otherwise the value of
// the step under the cursor. Drawing a step switches the lane on.
void cEuclidParams::changeShift(int16_t delta)
{
    const int step = delta > 0 ? 1 : -1;
    if (shiftSettings)
    {
        setShiftRowValue(selectedShiftRow, shiftRowValue(selectedShiftRow, false) + step);
        return;
    }

    euclid::Expression x = euclidEngine.getExpression(selectedChannel);
    if (shiftLane != 0)
    {
        euclid::Lane& l = x.lanes[shiftLane - 1];
        if (shiftCursor >= l.length)
            return;
        // "-" (the channel's own), then each value in turn.
        const int count = euclid::laneValueCount(kindOf(shiftLane));
        l.step[shiftCursor] = static_cast<int8_t>(clampInt(l.step[shiftCursor] + step, euclid::kLaneNone, count - 1));
        l.on = true;
    }
    else
    {
        euclid::Shift& s = x.shift;
        if (shiftCursor >= s.length)
            return;
        s.step[shiftCursor] =
          static_cast<int8_t>(clampInt(s.step[shiftCursor] + delta, -euclid::kMaxShift, euclid::kMaxShift));
        s.on = true;
    }
    euclidEngine.setExpression(selectedChannel, x);
    refreshShift();
    refreshTitle();
    refreshChannels();
    refreshRing();
    refreshPads();
}

void cEuclidParams::moveShiftCursor(int8_t delta)
{
    shiftCursor = static_cast<uint8_t>(clampInt(shiftCursor + delta, 0, shiftLaneLength() - 1));
    refreshShift();
    refreshPads();
}

void cEuclidParams::clearShiftStep(bool all)
{
    euclid::Expression x = euclidEngine.getExpression(selectedChannel);
    int8_t* steps = shiftLane == 0 ? x.shift.step : x.lanes[shiftLane - 1].step;
    const int8_t clear = shiftLane == 0 ? 0 : euclid::kLaneNone;
    if (all)
    {
        for (int i = 0; i < euclid::kShiftSteps; i++)
            steps[i] = clear;
    }
    else if (shiftCursor < euclid::kShiftSteps)
    {
        steps[shiftCursor] = clear;
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
    // The lanes as tabs: which one is shown, and which are on.
    panelData.tabCount = kLanes;
    panelData.tabSelected = static_cast<int8_t>(shiftLane);
    panelData.tabOn = 0;
    for (uint8_t i = 0; i < kLanes; i++)
    {
        panelData.tabs[i] = kLaneTabs[i];
        const bool laneOn = i == 0 ? x.shift.on : x.lanes[i - 1].on;
        if (laneOn)
            panelData.tabOn = static_cast<uint8_t>(panelData.tabOn | (1u << i));
    }
    const euclid::Shift& s = x.shift;
    const euclid::Lane* l = shiftLane != 0 ? &x.lanes[shiftLane - 1] : nullptr;
    const bool on = l != nullptr ? l->on : s.on;
    const uint8_t length = l != nullptr ? l->length : s.length;
    const euclid::ShiftAdvance advance = l != nullptr ? l->advance : s.advance;

    if (shiftSettings)
    {
        const uint8_t rows = shiftRows();
        panelData.mode = strEuclidPanelData::Mode::list;
        panelData.rows = rows;
        if (selectedShiftRow >= rows)
            selectedShiftRow = static_cast<uint8_t>(rows - 1);
        for (uint8_t i = 0; i < strEuclidPanelData::kMaxRows; i++)
        {
            panelData.names[i] = i < rows ? kShiftRowNames[i] : nullptr;
            panelData.values[i] = i < rows ? shiftText[i] : nullptr;
        }
        snprintf(shiftText[shiftRowLane], sizeof(shiftText[0]), "%s", kLaneNames[shiftLane]);
        snprintf(shiftText[shiftRowOn], sizeof(shiftText[0]), "%s", on ? "On" : "Off");
        snprintf(shiftText[shiftRowLength], sizeof(shiftText[0]), "%d steps", length);
        snprintf(shiftText[shiftRowAdvance], sizeof(shiftText[0]), "%s", advanceName(advance, true));
        snprintf(shiftText[shiftRowUnits], sizeof(shiftText[0]), "%s",
                 s.units == euclid::ShiftUnits::semitones ? "Semitones" : "Scale steps");
        panelData.meterCount = 0;
        uint32_t dim = on ? 0 : ((1ul << shiftRowLength) | (1ul << shiftRowAdvance) | (1ul << shiftRowUnits));
        if (isDivision(shiftLane))
            dim |= 1ul << shiftRowAdvance; // always per cycle
        panelData.dimRows = dim;
        panelData.stagedRows = stagedRowMask();
        refreshFrame();
        return;
    }

    // The steps: one cell per step, steps past the length shown as dashes.
    panelData.mode = strEuclidPanelData::Mode::grid;
    panelData.columns = 8;
    panelData.cells = euclid::kShiftSteps;
    panelData.usedCells = length;
    for (uint8_t i = 0; i < strEuclidPanelData::kMaxHelpLines; i++)
        panelData.help[i] = kShiftHelp[i];

    uint32_t dim = 0;
    for (uint8_t i = 0; i < euclid::kShiftSteps; i++)
    {
        bool none;
        if (l != nullptr)
        {
            snprintf(shiftCellText[i], sizeof(shiftCellText[i]), "%s",
                     euclid::laneValueName(kindOf(shiftLane), l->step[i]));
            none = l->step[i] == euclid::kLaneNone;
        }
        else
        {
            if (s.step[i] == 0)
                snprintf(shiftCellText[i], sizeof(shiftCellText[i]), "0");
            else
                snprintf(shiftCellText[i], sizeof(shiftCellText[i]), "%+d", s.step[i]);
            none = s.step[i] == 0;
        }
        panelData.cellText[i] = i < length ? shiftCellText[i] : nullptr;
        if (none || !on)
            dim |= 1ul << i;
    }
    panelData.dimCells = dim;
    if (shiftCursor >= length)
        shiftCursor = static_cast<uint8_t>(length - 1);

    const int8_t played = shiftPlayedStep();
    panelData.playingCell = on ? played : -1;
    lastShiftPlayed = played;

    if (!on)
        snprintf(noteHeaderText, sizeof(noteHeaderText), "%s off: turn a step to start", kLaneNames[shiftLane]);
    else if (l != nullptr)
        snprintf(noteHeaderText, sizeof(noteHeaderText), "%s: %d steps, %s", kLaneNames[shiftLane], length,
                 advanceName(isDivision(shiftLane) ? euclid::ShiftAdvance::perCycle : advance, false));
    else
        snprintf(noteHeaderText, sizeof(noteHeaderText), "%d steps, %s, %s", length, advanceName(advance, false),
                 s.units == euclid::ShiftUnits::semitones ? "semis" : "scale");
    panelData.header = noteHeaderText;
    refreshFrame();
}
