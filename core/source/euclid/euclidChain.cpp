#include "euclidChain.h"

#include "euclidSets.h"

namespace euclid
{

uint32_t Chain::totalBars() const
{
    uint32_t total = 0;
    for (uint8_t i = 0; i < count && i < kChainSteps; i++)
        total += steps[i].bars;
    return total;
}

int chainStepAt(const Chain& c, uint32_t bar, uint32_t* stepStart)
{
    const uint32_t total = c.totalBars();
    if (total == 0)
        return -1;
    uint32_t base = 0;
    if (bar >= total)
    {
        if (!c.loop)
            return -1;
        base = bar - bar % total;
        bar %= total;
    }
    uint32_t start = 0;
    for (uint8_t i = 0; i < c.count; i++)
    {
        if (bar < start + c.steps[i].bars)
        {
            if (stepStart != nullptr)
                *stepStart = base + start;
            return i;
        }
        start += c.steps[i].bars;
    }
    return -1;
}

size_t serializeChain(const Chain& c, uint8_t* buf, size_t length)
{
    if (buf == nullptr || length < kChainFileSize)
        return 0;
    for (size_t i = 0; i < kChainFileSize; i++)
        buf[i] = 0;
    buf[0] = 'E';
    buf[1] = 'U';
    buf[2] = 'C';
    buf[3] = 'H';
    buf[4] = kChainVersion;
    buf[5] = c.on ? 1 : 0;
    buf[6] = c.loop ? 1 : 0;
    buf[7] = c.count;
    for (int i = 0; i < kChainSteps; i++)
    {
        uint8_t* p = buf + 8 + i * 3;
        p[0] = c.steps[i].set;
        p[1] = c.steps[i].scene;
        p[2] = c.steps[i].bars;
    }
    const uint32_t crc = crc32(buf, kChainFileSize - 4);
    for (int i = 0; i < 4; i++)
        buf[kChainFileSize - 4 + i] = static_cast<uint8_t>(crc >> (8 * i));
    return kChainFileSize;
}

bool deserializeChain(Chain& c, const uint8_t* buf, size_t length)
{
    if (buf == nullptr || length != kChainFileSize || buf[0] != 'E' || buf[1] != 'U' || buf[2] != 'C' ||
        buf[3] != 'H' || buf[4] != kChainVersion)
        return false;
    uint32_t crc = 0;
    for (int i = 0; i < 4; i++)
        crc |= static_cast<uint32_t>(buf[kChainFileSize - 4 + i]) << (8 * i);
    if (crc != crc32(buf, kChainFileSize - 4))
        return false;

    Chain out;
    out.on = buf[5] != 0;
    out.loop = buf[6] != 0;
    out.count = buf[7] >= 1 && buf[7] <= kChainSteps ? buf[7] : 1;
    for (int i = 0; i < kChainSteps; i++)
    {
        const uint8_t* p = buf + 8 + i * 3;
        ChainStep& s = out.steps[i];
        s.set = p[0] <= kSetSlots ? p[0] : 0;
        s.scene = p[1] <= 4 ? p[1] : 0;
        s.bars = p[2] >= 1 && p[2] <= kChainMaxBars ? p[2] : 4;
    }
    c = out;
    return true;
}

} // namespace euclid
