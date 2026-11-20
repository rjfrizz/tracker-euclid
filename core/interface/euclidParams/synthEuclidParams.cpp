// The Synth page (Instr, or Shift + Params): the selected channel's hardware
// synth. The first rows choose the synth (a definition file in Synths/), its
// device number, the port, the part (for multitimbral synths) and a group of
// parameters; the rest are that group's parameters.
// Edits go out over SysEx straight away. Insert sends the whole patch;
// Shift + Insert gets the synth's patch.

#include "euclidParams/euclidParams.h"

#include "euclid/euclidPlatform.h"
#include "euclid/synthDef.h"

#include <stdio.h>

namespace
{
constexpr uint8_t kPads = 48;

enum class Header : uint8_t
{
    synth,
    device,
    port,
    part,
    group,
    none, // a parameter
};

uint8_t headerRows(const euclid::synth::Definition* d)
{
    return d == nullptr ? 1 : (d->partCount > 0 ? 5 : 4);
}

Header headerAt(const euclid::synth::Definition* d, uint8_t row)
{
    if (row >= headerRows(d))
        return Header::none;
    if (row == 3 && d->partCount > 0)
        return Header::part;
    if (row == headerRows(d) - 1 && row > 0)
        return Header::group;
    return static_cast<Header>(row);
}

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
    uint16_t rows = headerRows(d) + params;
    return static_cast<uint8_t>(rows > strEuclidPanelData::kMaxRows ? strEuclidPanelData::kMaxRows : rows);
}

// The definition's index of the param on a row, or -1 for a header row.
int cEuclidParams::synthParamAt(uint8_t row) const
{
    const euclid::synth::Definition* d = euclidSynth::definition(selectedChannel);
    const uint8_t header = headerRows(d);
    if (d == nullptr || row < header)
        return -1;
    uint8_t group = synthGroup[selectedChannel];
    if (group >= d->groupCount || row - header >= d->groups[group].count)
        return -1;
    return d->groups[group].first + (row - header);
}

cEuclidParams::PadMap cEuclidParams::synthPadMap(uint8_t row) const
{
    const euclid::synth::Definition* d = euclidSynth::definition(selectedChannel);
    switch (headerAt(d, row))
    {
        case Header::synth: return { PadMap::list, static_cast<uint8_t>(euclidSynth::files() + 1), 0, 0 };
        case Header::device: return { PadMap::list, 16, 1, 0 };
        case Header::port: return { PadMap::list, 4, 0, 0 };
        case Header::part:
            // With Auto (0) first, for synths whose parts follow MIDI channels.
            return d->partChannel > 0 ? PadMap{ PadMap::list, static_cast<uint8_t>(d->partCount + 1), 0, 0 }
                                      : PadMap{ PadMap::list, d->partCount, 1, 0 };
        case Header::group: return { PadMap::list, d->groupCount, 0, 0 };
        case Header::none: break;
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
    switch (headerAt(euclidSynth::definition(selectedChannel), row))
    {
        case Header::synth: return euclidSynth::assignedFile(selectedChannel) + 1; // 0 = none
        case Header::device: return euclidSynth::device(selectedChannel);
        case Header::port: return euclidSynth::ports(selectedChannel);
        case Header::part: return euclidSynth::partSetting(selectedChannel);
        case Header::group: return synthGroup[selectedChannel];
        case Header::none: break;
    }
    int p = synthParamAt(row);
    return p < 0 ? kNoValue : euclidSynth::value(selectedChannel, static_cast<uint16_t>(p));
}

void cEuclidParams::setSynthRowValue(uint8_t row, int v)
{
    const uint8_t ch = selectedChannel;
    switch (headerAt(euclidSynth::definition(ch), row))
    {
        case Header::synth:
        {
            int file = clampInt(v, 0, euclidSynth::files()) - 1;
            if (!euclidSynth::assign(ch, file))
                showMessage(euclidSynth::error());
            synthGroup[ch] = 0;
            euclidSynth::saveAssignments();
            break;
        }
        case Header::device:
            euclidSynth::setDevice(ch, static_cast<uint8_t>(clampInt(v, 1, 16)));
            euclidSynth::saveAssignments();
            break;
        case Header::port:
            euclidSynth::setPorts(ch, static_cast<uint8_t>(clampInt(v, 0, 3)));
            euclidSynth::saveAssignments();
            break;
        case Header::part:
        {
            const euclid::synth::Definition* d = euclidSynth::definition(ch);
            euclidSynth::setPart(ch, static_cast<uint8_t>(clampInt(v, d->partChannel > 0 ? 0 : 1, d->partCount)));
            euclidSynth::saveAssignments();
            break;
        }
        case Header::group:
        {
            const euclid::synth::Definition* d = euclidSynth::definition(ch);
            if (d != nullptr)
                synthGroup[ch] = static_cast<uint8_t>(clampInt(v, 0, d->groupCount - 1));
            break;
        }
        case Header::none:
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

    static const char* const kHeaderNames[] = { "Synth", "Device", "Port", "Part", "Group" };
    for (uint8_t row = 0; row < panelData.rows; row++)
    {
        char* t = synthText[row];
        const size_t n = sizeof(synthText[row]);
        const Header h = headerAt(d, row);
        if (h != Header::none)
        {
            panelData.names[row] = kHeaderNames[static_cast<uint8_t>(h)];
            switch (h)
            {
                case Header::synth:
                    if (euclidSynth::files() == 0)
                        snprintf(t, n, "None in Synths/");
                    else
                        snprintf(t, n, "%s", d ? d->name : "None");
                    break;
                case Header::device: snprintf(t, n, "%d", euclidSynth::device(ch)); break;
                case Header::port: snprintf(t, n, "%s", kPortNames[euclidSynth::ports(ch) & 3]); break;
                case Header::part:
                    if (euclidSynth::partSetting(ch) != 0)
                        snprintf(t, n, "%d", euclidSynth::partSetting(ch));
                    else if (euclidSynth::part(ch) != 0)
                        snprintf(t, n, "Auto: %d", euclidSynth::part(ch));
                    else
                        snprintf(t, n, "Auto: none");
                    break;
                case Header::group: snprintf(t, n, "%s", d->str(d->groups[synthGroup[ch]].name)); break;
                case Header::none: break;
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
