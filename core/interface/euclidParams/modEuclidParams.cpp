// The Mod page (Fx 1): the selected channel's four modulators, A-D. Smooth
// shapes cycle at a musical rate; Euclidean shapes follow their own rhythm.
// See euclid::Mod. Only the rows that apply to the modulator are shown.

#include "euclidParams/euclidParams.h"

#include "Platform.h"
#include "TrackerImpl.h"

#include <stdio.h>

namespace
{
constexpr uint8_t kPads = 48;

const char* const kModRowNames[cEuclidParams::modRowCount] = {
    "Destination", "Shape",    "Amount",   "Rate",        "Phase",  "Target",    "Steps",
    "Pulses",      "Rotation", "Division", "Auto rotate", "Decay",  "CC number",
};

// Shapes in the order shown: smooth ones first.
const euclid::ModShape kShapeOrder[] = {
    euclid::ModShape::sine,         euclid::ModShape::triangle,     euclid::ModShape::sawUp,
    euclid::ModShape::sawDown,      euclid::ModShape::square,       euclid::ModShape::smoothRandom,
    euclid::ModShape::drift,        euclid::ModShape::randomSteps,  euclid::ModShape::euclidAccent,
    euclid::ModShape::euclidEnvelope, euclid::ModShape::euclidRandom,
};
constexpr int kShapeCount = sizeof(kShapeOrder) / sizeof(kShapeOrder[0]);
static_assert(kShapeCount == static_cast<int>(euclid::ModShape::count), "every shape listed");

int shapeIndex(euclid::ModShape s)
{
    for (int i = 0; i < kShapeCount; i++)
        if (kShapeOrder[i] == s)
            return i;
    return 0;
}

// Auto-rotate choices, in the order shown: off, every 1, 2, 4, 8 bars.
const uint8_t kAutoRotate[] = { 0, 1, 2, 4, 8 };
constexpr int kAutoRotateCount = sizeof(kAutoRotate) / sizeof(kAutoRotate[0]);

int autoRotateIndex(uint8_t bars)
{
    for (int i = 0; i < kAutoRotateCount; i++)
        if (kAutoRotate[i] == bars)
            return i;
    return 0;
}

int clampInt(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

bool targetsAMod(euclid::ModDest d)
{
    return d == euclid::ModDest::modAmount || d == euclid::ModDest::modRate;
}
} // namespace

// The rows that apply to this modulator, in order.
void cEuclidParams::buildModRows()
{
    const euclid::Mod& m = euclidEngine.getExpression(selectedChannel).mod[selectedMod];
    uint8_t n = 0;
    modVisible[n++] = modRowDest;
    if (m.dest != euclid::ModDest::off)
    {
        modVisible[n++] = modRowShape;
        modVisible[n++] = modRowAmount;
        if (targetsAMod(m.dest))
            modVisible[n++] = modRowTarget;
        if (euclid::isEuclidShape(m.shape))
        {
            modVisible[n++] = modRowSteps;
            modVisible[n++] = modRowPulses;
            modVisible[n++] = modRowRotation;
            modVisible[n++] = modRowDivision;
            modVisible[n++] = modRowAutoRotate;
            if (m.shape == euclid::ModShape::euclidEnvelope)
                modVisible[n++] = modRowLength;
        }
        else
        {
            modVisible[n++] = modRowRate;
            modVisible[n++] = modRowPhase;
        }
        if (m.dest == euclid::ModDest::cc)
            modVisible[n++] = modRowCc;
    }
    modVisibleCount = n;
    if (selectedModRow >= modVisibleCount)
        selectedModRow = static_cast<uint8_t>(modVisibleCount - 1);
}

cEuclidParams::PadMap cEuclidParams::modPadMap(uint8_t index) const
{
    const euclid::Mod& m = euclidEngine.getExpression(selectedChannel).mod[selectedMod];
    switch (modRowId(index))
    {
        case modRowDest: return { PadMap::list, static_cast<uint8_t>(euclid::ModDest::count), 0, 0 };
        case modRowShape: return { PadMap::list, kShapeCount, 0, 0 };
        case modRowAmount: return { PadMap::level, kPads, -100, 100 };
        case modRowRate: return { PadMap::list, static_cast<uint8_t>(euclid::ModRate::count), 0, 0 };
        case modRowPhase: return { PadMap::level, kPads, 0, 99 };
        case modRowTarget: return { PadMap::list, euclid::kModsPerChannel, 0, 0 };
        case modRowSteps: return { PadMap::list, euclid::kMaxSteps, 1, 0 };
        case modRowPulses: return { PadMap::list, static_cast<uint8_t>(m.steps + 1), 0, 0 };
        case modRowRotation: return { PadMap::list, m.steps, 0, 0 };
        case modRowDivision: return { PadMap::list, static_cast<uint8_t>(euclid::Division::count), 0, 0 };
        case modRowAutoRotate: return { PadMap::list, kAutoRotateCount, 0, 0 };
        case modRowLength: return { PadMap::list, euclid::kMaxSteps, 1, 0 };
        case modRowCc: return { PadMap::level, kPads, 0, 127 };
        default: return { PadMap::none, 0, 0, 0 };
    }
}

int cEuclidParams::modRowValue(uint8_t index, bool live) const
{
    const euclid::Expression x =
      live ? euclidEngine.getLiveExpression(selectedChannel) : euclidEngine.getExpression(selectedChannel);
    const euclid::Mod& m = x.mod[selectedMod];
    switch (modRowId(index))
    {
        case modRowDest: return static_cast<int>(m.dest);
        case modRowShape: return shapeIndex(m.shape);
        case modRowAmount: return m.amount;
        case modRowRate: return static_cast<int>(m.rate);
        case modRowPhase: return m.phase;
        case modRowTarget: return m.target;
        case modRowSteps: return m.steps;
        case modRowPulses: return m.pulses;
        case modRowRotation: return m.rotation;
        case modRowDivision: return static_cast<int>(m.division);
        case modRowAutoRotate: return autoRotateIndex(m.autoRotate);
        case modRowLength: return m.length;
        case modRowCc: return m.cc;
        default: return kNoValue;
    }
}

void cEuclidParams::setModRowValue(uint8_t index, int v)
{
    euclid::Expression x = euclidEngine.getExpression(selectedChannel);
    euclid::Mod& m = x.mod[selectedMod];
    const uint8_t row = modRowId(index);
    switch (row)
    {
        case modRowDest:
            m.dest = static_cast<euclid::ModDest>(clampInt(v, 0, static_cast<int>(euclid::ModDest::count) - 1));
            // Aimed at another modulator: not itself.
            if (targetsAMod(m.dest) && m.target == selectedMod)
                m.target = static_cast<uint8_t>((selectedMod + 1) % euclid::kModsPerChannel);
            break;
        case modRowShape: m.shape = kShapeOrder[clampInt(v, 0, kShapeCount - 1)]; break;
        case modRowAmount: m.amount = static_cast<int8_t>(clampInt(v, -100, 100)); break;
        case modRowRate:
            m.rate = static_cast<euclid::ModRate>(clampInt(v, 0, static_cast<int>(euclid::ModRate::count) - 1));
            break;
        case modRowPhase: m.phase = static_cast<uint8_t>(clampInt(v, 0, 99)); break;
        case modRowTarget: m.target = static_cast<uint8_t>(clampInt(v, 0, euclid::kModsPerChannel - 1)); break;
        case modRowSteps:
            m.steps = static_cast<uint8_t>(clampInt(v, 1, euclid::kMaxSteps));
            if (m.pulses > m.steps)
                m.pulses = m.steps;
            if (m.rotation >= m.steps)
                m.rotation = static_cast<uint8_t>(m.steps - 1);
            break;
        case modRowPulses: m.pulses = static_cast<uint8_t>(clampInt(v, 0, m.steps)); break;
        case modRowRotation: m.rotation = static_cast<uint8_t>(clampInt(v, 0, m.steps - 1)); break;
        case modRowDivision:
            m.division = static_cast<euclid::Division>(clampInt(v, 0, static_cast<int>(euclid::Division::count) - 1));
            break;
        case modRowAutoRotate: m.autoRotate = kAutoRotate[clampInt(v, 0, kAutoRotateCount - 1)]; break;
        case modRowLength: m.length = static_cast<uint8_t>(clampInt(v, 1, euclid::kMaxSteps)); break;
        case modRowCc: m.cc = static_cast<uint8_t>(clampInt(v, 0, 127)); break;
        default: return;
    }
    euclidEngine.setExpression(selectedChannel, x);

    // Destination and Shape change which rows there are: stay on the row
    // that was edited.
    buildModRows();
    for (uint8_t i = 0; i < modVisibleCount; i++)
        if (modVisible[i] == row)
            selectedModRow = i;

    refreshMod();
    refreshTitle();
    refreshChannels();
    refreshRing();
    refreshPads();
}

void cEuclidParams::changeMod(int16_t delta)
{
    const uint8_t row = modRowId(selectedModRow);
    const int step = delta > 0 ? 1 : -1;
    // Rows with long ranges follow the encoder's speed; lists move one at a time.
    const bool fine = row == modRowAmount || row == modRowCc || row == modRowPhase;
    setModRowValue(selectedModRow, modRowValue(selectedModRow, false) + (fine ? delta : step));
}

uint32_t cEuclidParams::modDimMask() const
{
    return 0; // rows that don't apply aren't shown at all
}

void cEuclidParams::refreshMod()
{
    if (view != View::mod)
        return;

    buildModRows();
    panelData.rows = modVisibleCount;
    for (uint8_t i = 0; i < strEuclidPanelData::kMaxRows; i++)
    {
        panelData.names[i] = nullptr;
        panelData.values[i] = nullptr;
    }

    const euclid::Mod& m = euclidEngine.getExpression(selectedChannel).mod[selectedMod];
    for (uint8_t i = 0; i < modVisibleCount; i++)
    {
        const uint8_t row = modVisible[i];
        char* t = modText[i];
        const size_t n = sizeof(modText[i]);
        switch (row)
        {
            case modRowDest: snprintf(t, n, "%s", euclid::modDestName(m.dest)); break;
            case modRowShape: snprintf(t, n, "%s", euclid::modShapeName(m.shape)); break;
            case modRowAmount:
                if (m.dest == euclid::ModDest::pitch)
                    snprintf(t, n, "%+d st", (m.amount * 24 + (m.amount >= 0 ? 50 : -50)) / 100);
                else
                    snprintf(t, n, "%+d%%", m.amount);
                break;
            case modRowRate: snprintf(t, n, "%s", euclid::modRateName(m.rate)); break;
            case modRowPhase: snprintf(t, n, "%d%%", m.phase); break;
            case modRowTarget: snprintf(t, n, "Mod %c", 'A' + m.target); break;
            case modRowSteps: snprintf(t, n, "%d", m.steps); break;
            case modRowPulses: snprintf(t, n, "%d", m.pulses); break;
            case modRowRotation: snprintf(t, n, "%d", m.rotation); break;
            case modRowDivision: snprintf(t, n, "%s", euclid::divisionName(m.division)); break;
            case modRowAutoRotate:
                if (m.autoRotate == 0)
                    snprintf(t, n, "Off");
                else
                    snprintf(t, n, "Every %d bar%s", m.autoRotate, m.autoRotate == 1 ? "" : "s");
                break;
            case modRowLength: snprintf(t, n, "%d steps", m.length); break;
            case modRowCc: snprintf(t, n, "CC %d", m.cc); break;
            default: t[0] = 0; break;
        }
        panelData.names[i] = kModRowNames[row];
        panelData.values[i] = t;
    }

    panelData.dimRows = 0;
    panelData.stagedRows = stagedRowMask();
    updateModMeters(true);
}

// The meters above the list: all four modulators, the edited one outlined.
void cEuclidParams::updateModMeters(bool force)
{
    if (view != View::mod)
        return;

    const uint32_t now = TrackerImpl::getInstance()->getPlatform()->getTimer()->millis();
    if (!force && now - lastMeterMs < 40)
        return;

    const euclid::Expression x = euclidEngine.getExpression(selectedChannel);
    bool changed = force;
    panelData.meterCount = euclid::kModsPerChannel;
    panelData.meterSelected = static_cast<int8_t>(selectedMod);
    for (int k = 0; k < euclid::kModsPerChannel; k++)
    {
        const bool active = x.mod[k].dest != euclid::ModDest::off;
        const bool bipolar = !euclid::isEuclidShape(x.mod[k].shape);
        int16_t v = active ? euclidEngine.getModValue(selectedChannel, k) : 0;
        int diff = v - panelData.meterValue[k];
        if (active != panelData.meterActive[k] || bipolar != panelData.meterBipolar[k] || diff > 15 || diff < -15)
            changed = true;
        panelData.meterActive[k] = active;
        panelData.meterBipolar[k] = bipolar;
        panelData.meterValue[k] = v;
    }

    if (changed)
    {
        lastMeterMs = now;
        refreshFrame();
    }
}
