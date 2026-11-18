// The Mod page (Fx 2): the selected channel's two modulators. Each has its
// own Euclidean rhythm and a shape that turns it into a value added to a
// destination; see euclid::Mod.

#include "euclidParams/euclidParams.h"

#include <stdio.h>

namespace
{
constexpr uint8_t kPads = 48;

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
} // namespace

cEuclidParams::PadMap cEuclidParams::modPadMap(uint8_t row) const
{
    const euclid::Mod& m = euclidEngine.getExpression(selectedChannel).mod[selectedMod];
    switch (row)
    {
        case modRowDest: return { PadMap::list, static_cast<uint8_t>(euclid::ModDest::count), 0, 0 };
        case modRowShape: return { PadMap::list, static_cast<uint8_t>(euclid::ModShape::count), 0, 0 };
        case modRowAmount: return { PadMap::level, kPads, -100, 100 };
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

int cEuclidParams::modRowValue(uint8_t row, bool live) const
{
    const euclid::Expression x =
      live ? euclidEngine.getLiveExpression(selectedChannel) : euclidEngine.getExpression(selectedChannel);
    const euclid::Mod& m = x.mod[selectedMod];
    switch (row)
    {
        case modRowDest: return static_cast<int>(m.dest);
        case modRowShape: return static_cast<int>(m.shape);
        case modRowAmount: return m.amount;
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

void cEuclidParams::setModRowValue(uint8_t row, int v)
{
    euclid::Expression x = euclidEngine.getExpression(selectedChannel);
    euclid::Mod& m = x.mod[selectedMod];
    switch (row)
    {
        case modRowDest:
            m.dest = static_cast<euclid::ModDest>(clampInt(v, 0, static_cast<int>(euclid::ModDest::count) - 1));
            break;
        case modRowShape:
            m.shape = static_cast<euclid::ModShape>(clampInt(v, 0, static_cast<int>(euclid::ModShape::count) - 1));
            break;
        case modRowAmount: m.amount = static_cast<int8_t>(clampInt(v, -100, 100)); break;
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

    refreshMod();
    refreshTitle();
    refreshChannels();
    refreshRing();
    refreshPads();
}

void cEuclidParams::changeMod(int16_t delta)
{
    const uint8_t row = selectedModRow;
    const int step = delta > 0 ? 1 : -1;
    // Rows with long ranges follow the encoder's speed; lists move one at a time.
    const bool fine = row == modRowAmount || row == modRowCc;
    setModRowValue(row, modRowValue(row, false) + (fine ? delta : step));
}

// Rows that don't apply to the modulator's shape or destination are dimmed.
uint32_t cEuclidParams::modDimMask() const
{
    const euclid::Mod& m = euclidEngine.getExpression(selectedChannel).mod[selectedMod];
    uint32_t dim = 0;
    if (m.dest == euclid::ModDest::off)
        return ~(1ul << modRowDest) & ((1ul << modRowCount) - 1);
    if (!euclid::modUsesPulses(m.shape))
        dim |= (1ul << modRowPulses) | (1ul << modRowRotation) | (1ul << modRowAutoRotate);
    if (m.shape == euclid::ModShape::accent || m.shape == euclid::ModShape::stepped)
        dim |= 1ul << modRowLength;
    if (m.dest != euclid::ModDest::cc)
        dim |= 1ul << modRowCc;
    return dim;
}

void cEuclidParams::refreshMod()
{
    if (view != View::mod)
        return;

    const euclid::Mod& m = euclidEngine.getExpression(selectedChannel).mod[selectedMod];
    for (uint8_t row = 0; row < modRowCount; row++)
    {
        char* t = modText[row];
        const size_t n = sizeof(modText[row]);
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
    }

    panelData.dimRows = modDimMask();
    panelData.stagedRows = stagedRowMask();
    refreshFrame();
}
