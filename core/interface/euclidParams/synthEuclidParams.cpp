// The Synth page (Params, or Instr): the selected channel's hardware
// synth. The first rows choose the synth (a definition file in Synths/), its
// device number, the port, the part (for multitimbral synths) and a group of
// parameters; the rest are that group's parameters.
// Edits go out over SysEx straight away. Insert sends the whole patch;
// Shift + Insert gets the synth's patch.

#include "euclidParams/euclidParams.h"

#include "euclid/euclidPlatform.h"
#include "euclid/synthDef.h"
#include "fileManager.h"
#include "mtAudioEngine.h"

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

// The Synth row: 0 none (plain MIDI), 1 the sampler, 2 on the definition
// files.
constexpr int kSynthRowSampler = 1;
constexpr int kSynthRowFirstFile = 2;

// With the sampler, the rows under Synth are its instrument's settings, as
// the stock instrument editor has them (envelopes and the rest stay there:
// Shift + Instr).
enum SampleRow : uint8_t
{
    sampleSynth = 0,
    sampleInstrument,
    sampleVolume,
    samplePan,
    sampleTune,
    sampleFine,
    sampleFilter,
    sampleCutoff,
    sampleResonance,
    sampleReverb,
    sampleDelay,
    sampleRowCount
};
const char* const kSampleRowNames[sampleRowCount] = { "Synth",  "Instrument", "Volume",    "Pan",
                                                      "Tune",   "Fine tune",  "Filter",    "Cutoff",
                                                      "Resonance", "Reverb send", "Delay send" };
const char* const kFilterNames[] = { "Off", "Low pass", "High pass", "Band pass" };

bool isSampler(uint8_t ch)
{
    return euclidEngine.getExpression(ch).output == euclid::ChannelOutput::sampler;
}

int sampleValue(const strInstrument& in, uint8_t row)
{
    switch (row)
    {
        case sampleVolume: return in.volume;
        case samplePan: return in.panning;
        case sampleTune: return in.tune;
        case sampleFine: return in.fineTune;
        case sampleFilter: return in.filterEnable ? 1 + in.filterType : 0;
        case sampleCutoff: return static_cast<int>(in.cutOff * 100.0f + 0.5f);
        case sampleResonance: return static_cast<int>(in.resonance * 100.0f / RESONANCE_MAX + 0.5f);
        case sampleReverb: return in.reverbSend;
        case sampleDelay: return in.delaySend;
        default: return 0;
    }
}

// Changes an instrument setting as the instrument editor does: the value,
// then a refresh for whichever tracks play it, then the project's to save.
void setSampleValue(uint8_t instrument, uint8_t row, int v)
{
    strInstrument& in = mtProject.instrument[instrument];
    uint32_t mask = 0;
    switch (row)
    {
        case sampleVolume:
            in.volume = static_cast<uint8_t>(clampInt(v, 0, MAX_INSTRUMENT_VOLUME));
            mask = VOLUME_MASK;
            break;
        case samplePan:
            in.panning = static_cast<int16_t>(clampInt(v, PANNING_MIN, PANNING_MAX));
            mask = PANNING_MASK;
            break;
        case sampleTune:
            in.tune = static_cast<int8_t>(clampInt(v, MIN_INSTRUMENT_TUNE, MAX_INSTRUMENT_TUNE));
            mask = TUNE_MASK;
            break;
        case sampleFine:
            in.fineTune = static_cast<int8_t>(clampInt(v, MIN_INSTRUMENT_FINETUNE, MAX_INSTRUMENT_FINETUNE));
            mask = FINETUNE_MASK;
            break;
        case sampleFilter:
            v = clampInt(v, 0, 3);
            in.filterEnable = v != 0;
            in.filterType = static_cast<uint8_t>(v == 0 ? lowPass : v - 1);
            mask = CUTOFF_MASK | RESONANCE_MASK;
            break;
        case sampleCutoff:
            in.cutOff = clampInt(v, 0, 100) / 100.0f;
            mask = CUTOFF_MASK;
            break;
        case sampleResonance:
            in.resonance = clampInt(v, 0, 100) * RESONANCE_MAX / 100.0f;
            mask = RESONANCE_MASK;
            break;
        case sampleReverb:
            in.reverbSend = static_cast<uint8_t>(clampInt(v, SEND_MIN, SEND_MAX));
            mask = REVERB_SEND_MASK;
            break;
        case sampleDelay:
            in.delaySend = static_cast<uint8_t>(clampInt(v, SEND_MIN, SEND_MAX));
            mask = DELAY_SEND_MASK;
            break;
        default: return;
    }
    for (uint8_t i = 0; i < 8; i++)
        instrumentPlayer[i].setRefreshBit(mask);
    newFileManager.setInstrumentStructChanged(instrument);
}

void formatSample(const strInstrument& in, uint8_t instrument, uint8_t row, char* t, size_t n)
{
    const int v = sampleValue(in, row);
    switch (row)
    {
        case sampleInstrument:
            if (in.isActive == 1 && in.sample.file_name[0] != 0)
                snprintf(t, n, "%d %s", instrument + 1, in.sample.file_name);
            else
                snprintf(t, n, "%d empty", instrument + 1);
            break;
        case samplePan:
            if (v == 50)
                snprintf(t, n, "Centre");
            else
                snprintf(t, n, "%s %d", v < 50 ? "L" : "R", v < 50 ? 50 - v : v - 50);
            break;
        case sampleTune: snprintf(t, n, "%+d", v); break;
        case sampleFine: snprintf(t, n, "%+d", v); break;
        case sampleFilter: snprintf(t, n, "%s", kFilterNames[clampInt(v, 0, 3)]); break;
        default: snprintf(t, n, "%d", v); break;
    }
}

// The channel's instrument (or output) chosen now, not at Rec: the Synth
// page's changes are heard straight away.
void setChannelSound(uint8_t ch, euclid::ChannelOutput output, int instrument)
{
    euclid::Expression x = euclidEngine.getExpression(ch);
    x.output = output;
    if (instrument >= 0)
        x.instrument = static_cast<uint8_t>(instrument);
    euclidEngine.setImmediate(true);
    euclidEngine.setExpression(ch, x);
    euclidEngine.setImmediate(false);
}
} // namespace

uint8_t cEuclidParams::synthRowCount() const
{
    if (!synthPatches && isSampler(selectedChannel))
        return sampleRowCount;
    if (synthPatches)
    {
        const uint8_t left = static_cast<uint8_t>(euclidSynth::patches() - patchFirst);
        return euclidSynth::patches() == 0 ? 1 : (left > strEuclidPanelData::kMaxRows ? strEuclidPanelData::kMaxRows : left);
    }
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
    if (synthPatches)
        return { PadMap::none, 0, 0, 0 };
    if (row == 0)
        return { PadMap::list, static_cast<uint8_t>(euclidSynth::files() + kSynthRowFirstFile), 0, 0 };
    if (isSampler(selectedChannel))
    {
        switch (row)
        {
            case sampleInstrument: return { PadMap::list, euclid::kSampleInstruments, 0, 0 };
            case samplePan: return { PadMap::level, kPads, PANNING_MIN, PANNING_MAX };
            case sampleTune: return { PadMap::level, kPads, MIN_INSTRUMENT_TUNE, MAX_INSTRUMENT_TUNE };
            case sampleFine: return { PadMap::level, kPads, MIN_INSTRUMENT_FINETUNE, MAX_INSTRUMENT_FINETUNE };
            case sampleFilter: return { PadMap::list, 4, 0, 0 };
            default: return { PadMap::level, kPads, 0, 100 };
        }
    }
    const euclid::synth::Definition* d = euclidSynth::definition(selectedChannel);
    switch (headerAt(d, row))
    {
        case Header::synth: break; // above
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
    if (synthPatches)
        return kNoValue;
    if (row == 0)
        return isSampler(selectedChannel) ? kSynthRowSampler
                                          : (euclidSynth::assignedFile(selectedChannel) < 0
                                               ? 0
                                               : euclidSynth::assignedFile(selectedChannel) + kSynthRowFirstFile);
    if (isSampler(selectedChannel))
    {
        const uint8_t in = euclidEngine.getExpression(selectedChannel).instrument;
        return row == sampleInstrument ? in : sampleValue(mtProject.instrument[in], row);
    }
    switch (headerAt(euclidSynth::definition(selectedChannel), row))
    {
        case Header::synth: return kNoValue; // above
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
    if (synthPatches)
        return;
    const uint8_t ch = selectedChannel;
    if (row == 0)
    {
        // None, Sampler or a synth definition.
        v = clampInt(v, 0, euclidSynth::files() + kSynthRowFirstFile - 1);
        if (v == kSynthRowSampler)
        {
            euclidSynth::assign(ch, -1);
            setChannelSound(ch, euclid::ChannelOutput::sampler, -1);
        }
        else
        {
            setChannelSound(ch, euclid::ChannelOutput::midi, -1);
            if (!euclidSynth::assign(ch, v == 0 ? -1 : v - kSynthRowFirstFile))
                showMessage(euclidSynth::error());
        }
        synthGroup[ch] = 0;
        euclidSynth::saveAssignments();
        refreshSynth();
        refreshPads();
        refreshParams();
        return;
    }
    if (isSampler(ch))
    {
        if (row == sampleInstrument)
            setChannelSound(ch, euclid::ChannelOutput::sampler, clampInt(v, 0, euclid::kSampleInstruments - 1));
        else
            setSampleValue(euclidEngine.getExpression(ch).instrument, row, v);
        refreshSynth();
        refreshPads();
        return;
    }
    switch (headerAt(euclidSynth::definition(ch), row))
    {
        case Header::synth: break; // above
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
    if (synthPatches)
    {
        movePatchCursor(delta > 0 ? 1 : -1);
        return;
    }
    const uint8_t row = selectedSynthRow;
    const int step = delta > 0 ? 1 : -1;
    if (row != 0 && isSampler(selectedChannel))
    {
        // The wide ranges follow the encoder's speed.
        const bool fine = row != sampleInstrument && row != sampleFilter && row != sampleTune;
        setSynthRowValue(row, synthRowValue(row) + (fine ? delta : step));
        return;
    }
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
    if (synthPatches)
    {
        // The patch list: names only, a screenful at a time.
        panelData.rows = synthRowCount();
        for (uint8_t i = 0; i < strEuclidPanelData::kMaxRows; i++)
        {
            panelData.names[i] = nullptr;
            panelData.values[i] = nullptr;
        }
        if (euclidSynth::patches() == 0)
            panelData.names[0] = d != nullptr ? "None: Shift+File saves" : "No synth here";
        else
            for (uint8_t row = 0; row < panelData.rows; row++)
                panelData.names[row] = euclidSynth::patch(static_cast<uint8_t>(patchFirst + row));
        selectedSynthRow = static_cast<uint8_t>(patchCursor - patchFirst);
        panelData.dimRows = 0;
        panelData.stagedRows = 0;
        panelData.meterCount = 0;
        refreshFrame();
        return;
    }
    if (isSampler(ch))
    {
        const uint8_t in = euclidEngine.getExpression(ch).instrument;
        panelData.rows = sampleRowCount;
        if (selectedSynthRow >= sampleRowCount)
            selectedSynthRow = sampleRowCount - 1;
        for (uint8_t i = 0; i < strEuclidPanelData::kMaxRows; i++)
        {
            panelData.names[i] = i < sampleRowCount ? kSampleRowNames[i] : nullptr;
            panelData.values[i] = i < sampleRowCount ? synthText[i] : nullptr;
        }
        snprintf(synthText[0], sizeof(synthText[0]), "Sampler");
        for (uint8_t row = 1; row < sampleRowCount; row++)
            formatSample(mtProject.instrument[in], in, row, synthText[row], sizeof(synthText[row]));
        // The filter's settings don't apply while it's off.
        panelData.dimRows =
          mtProject.instrument[in].filterEnable ? 0 : ((1ul << sampleCutoff) | (1ul << sampleResonance));
        panelData.stagedRows = 0;
        panelData.meterCount = 0;
        refreshFrame();
        return;
    }
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
                case Header::synth: snprintf(t, n, "%s", d ? d->name : "None (MIDI)"); break;
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

//------------------------------------------------------------------------------
// The patch list (File on the Synth page).

void cEuclidParams::openPatches(bool open)
{
    synthPatches = open && euclidSynth::definition(selectedChannel) != nullptr;
    if (open && !synthPatches)
        showMessage("No synth on this channel");
    if (synthPatches)
    {
        euclidSynth::scanPatches(selectedChannel);
        patchCursor = 0;
        patchFirst = 0;
    }
    selectedSynthRow = 0;
    refreshTitle();
    refreshSynth();
    refreshPads();
}

void cEuclidParams::movePatchCursor(int delta)
{
    const int count = euclidSynth::patches();
    if (count == 0)
        return;
    int c = patchCursor + delta;
    c = c < 0 ? 0 : (c >= count ? count - 1 : c);
    patchCursor = static_cast<uint8_t>(c);
    // Keep it on screen.
    const int rows = strEuclidPanelData::kMaxRows;
    if (patchCursor < patchFirst)
        patchFirst = patchCursor;
    else if (patchCursor >= patchFirst + rows)
        patchFirst = static_cast<uint8_t>(patchCursor - rows + 1);
    refreshSynth();
}

void cEuclidParams::savePatch()
{
    char name[24];
    char text[48];
    if (euclidSynth::savePatch(selectedChannel, name, sizeof(name)))
        snprintf(text, sizeof(text), "Saved %s", name);
    else
        snprintf(text, sizeof(text), "%s", euclidSynth::error());
    showMessage(text);
    if (synthPatches)
        refreshSynth();
}

void cEuclidParams::loadPatch()
{
    if (euclidSynth::patches() == 0)
        return;
    char text[48];
    if (euclidSynth::loadPatch(selectedChannel, patchCursor))
        snprintf(text, sizeof(text), "Sending %s", euclidSynth::patch(patchCursor));
    else
        snprintf(text, sizeof(text), "%s", euclidSynth::error());
    showMessage(text);
    refreshChannels(); // the channel takes the patch's name
}

void cEuclidParams::deletePatch()
{
    if (euclidSynth::patches() == 0)
        return;
    char text[48];
    snprintf(text, sizeof(text), "Deleted %s", euclidSynth::patch(patchCursor));
    if (!euclidSynth::deletePatch(selectedChannel, patchCursor))
        snprintf(text, sizeof(text), "Couldn't delete it");
    if (patchCursor >= euclidSynth::patches() && patchCursor > 0)
        patchCursor--;
    if (patchFirst > patchCursor)
        patchFirst = patchCursor;
    showMessage(text);
    refreshSynth();
}
