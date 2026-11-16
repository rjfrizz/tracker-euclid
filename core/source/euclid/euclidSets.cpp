#include "euclidSets.h"

namespace euclid
{

uint32_t crc32(const uint8_t* data, size_t length)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < length; i++)
    {
        crc ^= data[i];
        for (int b = 0; b < 8; b++)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

namespace
{
void putU32(uint8_t* p, uint32_t v)
{
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
    p[2] = static_cast<uint8_t>(v >> 16);
    p[3] = static_cast<uint8_t>(v >> 24);
}

uint32_t getU32(const uint8_t* p)
{
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

constexpr size_t kCrcOffset = kSetFileSize - 4;
} // namespace

size_t serializeSet(const Engine& engine, uint8_t slot, uint8_t* buf, size_t bufLen)
{
    if (buf == nullptr || bufLen < kSetFileSize)
        return 0;

    for (size_t i = 0; i < kSetFileSize; i++)
        buf[i] = 0;

    buf[0] = 'E';
    buf[1] = 'U';
    buf[2] = 'C';
    buf[3] = 'S';
    buf[4] = kSetVersion;
    buf[5] = kChannels;
    buf[6] = slot <= kSetSlots ? slot : 0;

    for (int ch = 0; ch < kChannels; ch++)
    {
        Rhythm r = engine.getRhythm(ch);
        Expression x = engine.getExpression(ch);
        uint8_t* p = buf + kSetHeaderSize + ch * kSetChannelSize;

        p[0] = r.steps;
        p[1] = r.pulses;
        p[2] = r.rotation;
        p[3] = static_cast<uint8_t>(r.division);
        p[4] = static_cast<uint8_t>(r.direction);
        p[5] = r.noteCount;
        for (int n = 0; n < kMaxNotes; n++)
            p[6 + n] = r.notes[n];
        p[22] = x.enabled ? 1 : 0;
        p[23] = x.muted ? 1 : 0;
        p[24] = x.midiChannel;
        p[25] = x.velocity;
        p[26] = x.gate;
        p[27] = x.probability;
        // 28-31 reserved
    }

    putU32(buf + kCrcOffset, crc32(buf, kCrcOffset));
    return kSetFileSize;
}

bool deserializeSet(Engine& engine, const uint8_t* buf, size_t len, uint8_t* slotOut)
{
    if (buf == nullptr || len != kSetFileSize)
        return false;
    if (buf[0] != 'E' || buf[1] != 'U' || buf[2] != 'C' || buf[3] != 'S')
        return false;
    if (buf[4] != kSetVersion || buf[5] != kChannels)
        return false;
    if (getU32(buf + kCrcOffset) != crc32(buf, kCrcOffset))
        return false;

    for (int ch = 0; ch < kChannels; ch++)
    {
        const uint8_t* p = buf + kSetHeaderSize + ch * kSetChannelSize;

        Rhythm r;
        r.steps = p[0];
        r.pulses = p[1];
        r.rotation = p[2];
        r.division = p[3] < static_cast<uint8_t>(Division::count) ? static_cast<Division>(p[3])
                                                                  : Division::d1_16;
        r.direction = p[4] < static_cast<uint8_t>(NoteDirection::count)
                        ? static_cast<NoteDirection>(p[4])
                        : NoteDirection::forward;
        r.noteCount = p[5];
        for (int n = 0; n < kMaxNotes; n++)
            r.notes[n] = p[6 + n];

        Expression x;
        x.enabled = p[22] != 0;
        x.muted = p[23] != 0;
        x.midiChannel = p[24];
        x.velocity = p[25];
        x.gate = p[26];
        x.probability = p[27];

        // The engine clamps every field, so a hand-edited file can't put it
        // in a bad state.
        engine.setRhythm(ch, r);
        engine.setExpression(ch, x);
    }

    if (slotOut != nullptr)
        *slotOut = buf[6] <= kSetSlots ? buf[6] : 0;
    return true;
}

} // namespace euclid
