// The Synth page (Instr): the selected channel's hardware synth. The first
// rows choose the synth (a definition file in Synths/), its device number,
// the port and a group of parameters; the rest are that group's parameters.
// Edits go out over SysEx straight away. Insert sends the whole patch.

#include "euclidParams/euclidParams.h"

#include "euclid/euclidPlatform.h"
#include "euclid/synthDef.h"

#include <stdio.h>

namespace
{
constexpr uint8_t kPads = 48;
constexpr uint8_t kHeaderRows = 4; // Synth, Device, Port, Group

const char* const kPortNames[] = { "Notes port", "DIN", "USB", "DIN + USB" };

int clampInt(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}
} // namespace

uint8_t cEuclidParams::synthRowCount() const
{
    const euclid::synth::Definition* d = euclidSynth::definition(selectedChannel);
    if (d == nullptr)
        return 1; // just the Synth row
    uint8_t group = synthGroup[selectedChannel];
    uint16_t params = group < d->groupCount ? d->groups[group].count : 0;
    uint16_t rows = kHeaderRows + params;
    return static_cast<uint8_t>(rows > strEuclidPanelData::kMaxRows ? strEuclidPanelData::kMaxRows : rows);
}

// The definition's index of the param on a row, or -1 for a header row.
int cEuclidParams::synthParamAt(uint8_t row) const
{
    const euclid::synth::Definition* d = euclidSynth::definition(selectedChannel);
    if (d == nullptr || row < kHeaderRows)
        return -1;
    uint8_t group = synthGroup[selectedChannel];
    if (group >= d->groupCount || row - kHeaderRows >= d->groups[group].count)
        return -1;
    return d->groups[group].first + (row - kHeaderRows);
}

cEuclidParams::PadMap cEuclidParams::synthPadMap(uint8_t row) const
{
    const euclid::synth::Definition* d = euclidSynth::definition(selectedChannel);
    switch (row)
    {
        case 0: return { PadMap::list, static_cast<uint8_t>(euclidSynth::files() + 1), 0, 0 };
        case 1: return { PadMap::list, 16, 1, 0 };
        case 2: return { PadMap::list, 4, 0, 0 };
        case 3: return { PadMap::list, d ? d->groupCount : static_cast<uint8_t>(0), 0, 0 };
        default: break;
    }
    int p = synthParamAt(row);
    if (p < 0)
        return { PadMap::none, 0, 0, 0 };
    const auto& prm = d->params[p];
    int span = prm.max - prm.min + 1;
    if (span <= kPads)
        return { PadMap::list, static_cast<uint8_t>(span), prm.min, 0 };
    return { PadMap::level, kPads, prm.min, prm.max };
}

int cEuclidParams::synthRowValue(uint8_t row) const
{
    switch (row)
    {
        case 0: return euclidSynth::assignedFile(selectedChannel) + 1; // 0 = none
        case 1: return euclidSynth::device(selectedChannel);
        case 2: return euclidSynth::ports(selectedChannel);
        case 3: return synthGroup[selectedChannel];
        default: break;
    }
    int p = synthParamAt(row);
    return p < 0 ? kNoValue : euclidSynth::value(selectedChannel, static_cast<uint16_t>(p));
}

void cEuclidParams::setSynthRowValue(uint8_t row, int v)
{
    const uint8_t ch = selectedChannel;
    switch (row)
    {
        case 0:
        {
            int file = clampInt(v, 0, euclidSynth::files()) - 1;
            if (!euclidSynth::assign(ch, file))
                showMessage(euclidSynth::error());
            synthGroup[ch] = 0;
            euclidSynth::saveAssignments();
            break;
        }
        case 1:
            euclidSynth::setDevice(ch, static_cast<uint8_t>(clampInt(v, 1, 16)));
            euclidSynth::saveAssignments();
            break;
        case 2:
            euclidSynth::setPorts(ch, static_cast<uint8_t>(clampInt(v, 0, 3)));
            euclidSynth::saveAssignments();
            break;
        case 3:
        {
            const euclid::synth::Definition* d = euclidSynth::definition(ch);
            if (d != nullptr)
                synthGroup[ch] = static_cast<uint8_t>(clampInt(v, 0, d->groupCount - 1));
            break;
        }
        default:
        {
            int p = synthParamAt(row);
            if (p >= 0)
                euclidSynth::setValue(ch, static_cast<uint16_t>(p), v);
            break;
        }
    }
    refreshSynth();
    refreshPads();
}

void cEuclidParams::changeSynth(int16_t delta)
{
    const uint8_t row = selectedSynthRow;
    const int step = delta > 0 ? 1 : -1;
    int p = synthParamAt(row);
    // Parameters with long ranges follow the encoder's speed.
    bool fine = false;
    if (p >= 0)
    {
        const auto& prm = euclidSynth::definition(selectedChannel)->params[p];
        fine = prm.max - prm.min > 31;
    }
    setSynthRowValue(row, synthRowValue(row) + (fine ? delta : step));
}

void cEuclidParams::refreshSynth()
{
    if (view != View::synth)
        return;

    const uint8_t ch = selectedChannel;
    const euclid::synth::Definition* d = euclidSynth::definition(ch);
    panelData.rows = synthRowCount();
    if (selectedSynthRow >= panelData.rows)
        selectedSynthRow = static_cast<uint8_t>(panelData.rows - 1);
    for (uint8_t i = 0; i < strEuclidPanelData::kMaxRows; i++)
    {
        panelData.names[i] = nullptr;
        panelData.values[i] = nullptr;
    }

    static const char* const kHeaderNames[kHeaderRows] = { "Synth", "Device", "Port", "Group" };
    for (uint8_t row = 0; row < panelData.rows; row++)
    {
        char* t = synthText[row];
        const size_t n = sizeof(synthText[row]);
        if (row < kHeaderRows)
        {
            panelData.names[row] = kHeaderNames[row];
            switch (row)
            {
                case 0:
                    if (euclidSynth::files() == 0)
                        snprintf(t, n, "None in Synths/");
                    else
                        snprintf(t, n, "%s", d ? d->name : "None");
                    break;
                case 1: snprintf(t, n, "%d", euclidSynth::device(ch)); break;
                case 2: snprintf(t, n, "%s", kPortNames[euclidSynth::ports(ch) & 3]); break;
                case 3: snprintf(t, n, "%s", d ? d->str(d->groups[synthGroup[ch]].name) : "-"); break;
            }
        }
        else
        {
            int p = synthParamAt(row);
            panelData.names[row] = d->str(d->params[p].name);
            euclid::synth::formatValue(*d, static_cast<uint16_t>(p), euclidSynth::value(ch, static_cast<uint16_t>(p)),
                                       t, n);
        }
        panelData.values[row] = t;
    }

    panelData.dimRows = 0;
    panelData.stagedRows = 0; // synth edits aren't staged
    panelData.meterCount = 0;
    refreshFrame();
}
