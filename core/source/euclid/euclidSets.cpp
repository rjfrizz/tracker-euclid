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

template <typename E> E enumOr(uint8_t v, E count, E fallback)
{
    return v < static_cast<uint8_t>(count) ? static_cast<E>(v) : fallback;
}
} // namespace

size_t serializeSet(const Engine& engine, uint8_t slot, uint8_t* buf, size_t bufLen)
{
    if (buf == nullptr || bufLen < kSetFileSize)
        return 0;

    for (size_t i = 0; i < kSetFileSize; i++)
        buf[i] = 0;

    const Key key = engine.getGlobalKey();
    buf[0] = 'E';
    buf[1] = 'U';
    buf[2] = 'C';
    buf[3] = 'S';
    buf[4] = kSetVersion;
    buf[5] = kChannels;
    buf[6] = slot <= kSetSlots ? slot : 0;
    buf[7] = key.root;
    buf[8] = key.scale;
    buf[9] = static_cast<uint8_t>(engine.getLaunch());

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
        p[28] = static_cast<uint8_t>(r.mode);
        p[29] = static_cast<uint8_t>(x.keyMode);
        p[30] = x.keyOffset;
        p[31] = x.ownRoot;
        p[32] = x.ownScale;
        p[33] = x.octave;
        p[34] = x.range;
    }

    const size_t crcOffset = kSetFileSize - 4;
    putU32(buf + crcOffset, crc32(buf, crcOffset));
    return kSetFileSize;
}

bool deserializeSet(Engine& engine, const uint8_t* buf, size_t len, uint8_t* slotOut)
{
    if (buf == nullptr || len < 8)
        return false;
    if (buf[0] != 'E' || buf[1] != 'U' || buf[2] != 'C' || buf[3] != 'S')
        return false;
    if (buf[5] != kChannels)
        return false;

    const uint8_t version = buf[4];
    size_t headerSize, channelSize, fileSize;
    if (version == 1)
    {
        headerSize = kSetV1HeaderSize;
        channelSize = kSetV1ChannelSize;
        fileSize = kSetV1FileSize;
    }
    else if (version == 2)
    {
        headerSize = kSetHeaderSize;
        channelSize = kSetChannelSize;
        fileSize = kSetFileSize;
    }
    else
    {
        return false;
    }

    if (len != fileSize)
        return false;
    const size_t crcOffset = fileSize - 4;
    if (getU32(buf + crcOffset) != crc32(buf, crcOffset))
        return false;

    // Everything is checked; from here on the engine is changed.
    if (version >= 2)
    {
        Key key;
        key.root = buf[7];
        key.scale = buf[8];
        engine.setGlobalKey(key);
        engine.setLaunch(enumOr(buf[9], Launch::count, Launch::bar));
    }

    for (int ch = 0; ch < kChannels; ch++)
    {
        const uint8_t* p = buf + headerSize + ch * channelSize;

        Rhythm r;
        r.steps = p[0];
        r.pulses = p[1];
        r.rotation = p[2];
        r.division = enumOr(p[3], Division::count, Division::d1_16);
        r.direction = enumOr(p[4], NoteDirection::count, NoteDirection::forward);
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

        if (version >= 2)
        {
            r.mode = enumOr(p[28], NoteMode::count, NoteMode::seq);
            x.keyMode = enumOr(p[29], KeyMode::count, KeyMode::follow);
            x.keyOffset = p[30];
            x.ownRoot = p[31];
            x.ownScale = p[32];
            x.octave = p[33];
            x.range = p[34];
        }

        // The engine clamps every field, so a hand-edited file can't put it
        // in a bad state.
        engine.setRhythm(ch, r);
        engine.setExpression(ch, x);
    }

    // A loaded set replaces what's playing as a whole, at the launch point.
    engine.commitAll();

    if (slotOut != nullptr)
        *slotOut = buf[6] <= kSetSlots ? buf[6] : 0;
    return true;
}

} // namespace euclid
