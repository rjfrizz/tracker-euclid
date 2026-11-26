#include "euclidSets.h"

#include "scales.h"

namespace euclid
{

namespace
{
// Where a lane is in a channel record: the first four together, the ones
// added in version 11 after the hand changes.
size_t laneOffset(int k)
{
    const int first = static_cast<int>(LaneKind::sample);
    return k < first ? kSetLanesOffset + k * kSetLaneSize : kSetSampleLaneOffset + (k - first) * kSetLaneSize;
}
} // namespace


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

    const Key key = engine.getSavedGlobalKey(); // a progression's home, not its chord
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
    buf[10] = static_cast<uint8_t>(engine.getFreezeLength());
    buf[11] = engine.getLayer() ? 1 : 0;

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
        p[35] = static_cast<uint8_t>(x.seqKeyMode);
        p[36] = static_cast<uint8_t>(x.transpose + 64);
        p[37] = r.homeRoot;
        p[38] = r.homeScale;
        p[39] = engine.getName(ch);
        p[40] = static_cast<uint8_t>(r.tieMask);
        p[41] = static_cast<uint8_t>(r.tieMask >> 8);
        p[42] = static_cast<uint8_t>(x.chord);
        p[43] = x.chordChance;
        p[44] = static_cast<uint8_t>(x.output);
        p[45] = x.instrument;
        for (int k = 0; k < kModsPerChannel; k++)
        {
            const Mod& m = x.mod[k];
            uint8_t* q = p + 48 + k * kSetModSize;
            q[0] = static_cast<uint8_t>(m.dest);
            q[1] = static_cast<uint8_t>(m.shape);
            q[2] = static_cast<uint8_t>(m.amount + 128);
            q[3] = static_cast<uint8_t>(m.rate);
            q[4] = m.phase;
            q[5] = m.target;
            q[6] = m.steps;
            q[7] = m.pulses;
            q[8] = m.rotation;
            q[9] = static_cast<uint8_t>(m.division);
            q[10] = m.autoRotate;
            q[11] = m.length;
            q[12] = m.cc;
            q[13] = static_cast<uint8_t>((m.delay & 15) | (m.fade << 4));
        }
        uint8_t* sh = p + kSetShiftOffset;
        sh[0] = x.shift.on ? 1 : 0;
        sh[1] = x.shift.length;
        sh[2] = static_cast<uint8_t>(x.shift.advance);
        sh[3] = static_cast<uint8_t>(x.shift.units);
        for (int i = 0; i < kShiftSteps; i++)
            sh[4 + i] = static_cast<uint8_t>(x.shift.step[i] + 64);
        const char* text = engine.getTextName(ch);
        for (int i = 0; i < Engine::kTextNameLength && text[i] != 0; i++)
            p[kSetTextNameOffset + i] = static_cast<uint8_t>(text[i]);
        for (int k = 0; k < kLaneKinds; k++)
        {
            const Lane& l = x.lanes[k];
            uint8_t* q = p + laneOffset(k);
            q[0] = l.on ? 1 : 0;
            q[1] = l.length;
            q[2] = static_cast<uint8_t>(l.advance);
            for (int i = 0; i < kShiftSteps; i++)
                q[3 + i] = static_cast<uint8_t>(l.step[i] + 1);
        }
        putU32(p + kSetForceOffset, r.forceOn);
        putU32(p + kSetForceOffset + 4, r.forceOff);
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
    else if (version == 2 || version == 3)
    {
        headerSize = kSetHeaderSize;
        channelSize = kSetV3ChannelSize;
        fileSize = kSetV3FileSize;
    }
    else if (version == 4)
    {
        headerSize = kSetHeaderSize;
        channelSize = kSetV4ChannelSize;
        fileSize = kSetV4FileSize;
    }
    else if (version == 5)
    {
        headerSize = kSetHeaderSize;
        channelSize = kSetV5ChannelSize;
        fileSize = kSetV5FileSize;
    }
    else if (version == 6)
    {
        headerSize = kSetHeaderSize;
        channelSize = kSetV6ChannelSize;
        fileSize = kSetV6FileSize;
    }
    else if (version == 7)
    {
        headerSize = kSetHeaderSize;
        channelSize = kSetV7ChannelSize;
        fileSize = kSetV7FileSize;
    }
    else if (version == 8)
    {
        headerSize = kSetHeaderSize;
        channelSize = kSetV8ChannelSize;
        fileSize = kSetV8FileSize;
    }
    else if (version == 9)
    {
        headerSize = kSetHeaderSize;
        channelSize = kSetV9ChannelSize;
        fileSize = kSetV9FileSize;
    }
    else if (version == 10)
    {
        headerSize = kSetHeaderSize;
        channelSize = kSetV10ChannelSize;
        fileSize = kSetV10FileSize;
    }
    else if (version == 11)
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
    Key fileKey; // version 1 files have none: the default
    if (version >= 2)
    {
        Key key;
        key.root = buf[7];
        key.scale = buf[8];
        fileKey = key;
        engine.setGlobalKey(key);
        engine.setLaunch(enumOr(buf[9], Launch::count, Launch::bar));
        engine.setFreezeLength(enumOr(buf[10], FreezeLength::count, FreezeLength::fourBars));
        engine.setLayer(buf[11] != 0);
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
        x.seqKeyMode = KeyMode::own; // files without it played their lists in their own key
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
        if (version >= 3)
        {
            x.seqKeyMode = enumOr(p[35], KeyMode::count, KeyMode::own);
            x.transpose = static_cast<int8_t>(static_cast<int>(p[36]) - 64);
            r.homeRoot = p[37];
            r.homeScale = p[38];
            engine.setName(ch, p[39]);
        }
        else
        {
            r.homeRoot = fileKey.root;
            r.homeScale = fileKey.scale;
            engine.setName(ch, 0);
        }
        if (version >= 4)
        {
            r.tieMask = static_cast<uint16_t>(p[40] | (p[41] << 8));
            x.chord = enumOr(p[42], ChordType::count, ChordType::single);
            x.chordChance = p[43];
            x.output = enumOr(p[44], ChannelOutput::count, ChannelOutput::midi);
            x.instrument = p[45] < kSampleInstruments ? p[45] : 0;
        }
        if (version >= 6)
        {
            for (int k = 0; k < kModsPerChannel; k++)
            {
                const uint8_t* q = p + 48 + k * kSetModSize;
                Mod& m = x.mod[k];
                m.dest = enumOr(q[0], ModDest::count, ModDest::off);
                m.shape = enumOr(q[1], ModShape::count, ModShape::sine);
                m.amount = static_cast<int8_t>(static_cast<int>(q[2]) - 128);
                m.rate = enumOr(q[3], ModRate::count, ModRate::bar1);
                m.phase = q[4];
                m.target = q[5];
                m.steps = q[6];
                m.pulses = q[7];
                m.rotation = q[8];
                m.division = enumOr(q[9], Division::count, Division::d1_16);
                m.autoRotate = q[10];
                m.length = q[11];
                m.cc = q[12];
                m.delay = q[13] & 15; // 0 in files from before delays
                m.fade = q[13] >> 4;
            }
        }
        if (version >= 7)
        {
            const uint8_t* sh = p + kSetShiftOffset;
            x.shift.on = sh[0] != 0;
            x.shift.length = sh[1];
            x.shift.advance = enumOr(sh[2], ShiftAdvance::count, ShiftAdvance::perStep);
            x.shift.units = enumOr(sh[3], ShiftUnits::count, ShiftUnits::semitones);
            for (int i = 0; i < kShiftSteps; i++)
                x.shift.step[i] = static_cast<int8_t>(static_cast<int>(sh[4 + i]) - 64);
        }
        if (version >= 10)
        {
            r.forceOn = getU32(p + kSetForceOffset);
            r.forceOff = getU32(p + kSetForceOffset + 4);
        }
        if (version >= 9)
        {
            // The Sample and Pulses lanes came in version 11, after the
            // hand changes.
            const int kinds = version >= 11 ? kLaneKinds : static_cast<int>(LaneKind::sample);
            for (int k = 0; k < kinds; k++)
            {
                const uint8_t* q = p + laneOffset(k);
                Lane& l = x.lanes[k];
                l.on = q[0] != 0;
                l.length = q[1];
                l.advance = enumOr(q[2], ShiftAdvance::count, ShiftAdvance::perCycle);
                for (int i = 0; i < kShiftSteps; i++)
                    l.step[i] = static_cast<int8_t>(static_cast<int>(q[3 + i]) - 1);
            }
        }
        if (version == 5)
        {
            // Two modulators; the shape codes are the same, and an LFO's
            // period was `length` of its steps.
            for (int k = 0; k < 2; k++)
            {
                const uint8_t* q = p + 48 + k * kSetV5ModSize;
                Mod& m = x.mod[k];
                m.dest = enumOr(q[0], ModDest::count, ModDest::off);
                m.shape = enumOr(q[1], ModShape::count, ModShape::sine);
                m.amount = static_cast<int8_t>(static_cast<int>(q[2]) - 128);
                m.steps = q[3];
                m.pulses = q[4];
                m.rotation = q[5];
                m.division = enumOr(q[6], Division::count, Division::d1_16);
                m.autoRotate = q[7];
                m.length = q[8];
                m.cc = q[9];
                m.rate = modRateNearest(static_cast<uint32_t>(m.length) * divisionToMicroSteps(m.division));
            }
        }

        // The engine clamps every field, so a hand-edited file can't put it
        // in a bad state.
        engine.setRhythm(ch, r);
        engine.setExpression(ch, x);

        // After setName (which clears it): the text name, from version 8.
        char text[Engine::kTextNameLength + 1] = {};
        if (version >= 8)
            for (int i = 0; i < Engine::kTextNameLength; i++)
                text[i] = static_cast<char>(p[kSetTextNameOffset + i]);
        engine.setTextName(ch, text);
        engine.setMutedAtLaunch(ch, x.muted);
    }

    // A loaded set replaces what's playing as a whole, at the launch point.
    engine.commitAll();

    if (slotOut != nullptr)
        *slotOut = buf[6] <= kSetSlots ? buf[6] : 0;
    return true;
}

//------------------------------------------------------------------------------
// User progressions (euclidEngine.h)

size_t serializeProgressions(const Progression* user, uint8_t* buf, size_t length)
{
    if (length < kProgFileSize)
        return 0;
    for (size_t i = 0; i < kProgFileSize; i++)
        buf[i] = 0;
    buf[0] = 'E';
    buf[1] = 'U';
    buf[2] = 'P';
    buf[3] = 'R';
    buf[4] = 1; // version
    buf[5] = kUserProgs;
    size_t at = 8;
    for (int u = 0; u < kUserProgs; u++)
    {
        for (int k = 0; k < kProgNameLength; k++)
            buf[at++] = static_cast<uint8_t>(user[u].name[k]);
        buf[at++] = user[u].count;
        for (int k = 0; k < kProgSlots; k++)
        {
            buf[at++] = user[u].slot[k].root;
            buf[at++] = user[u].slot[k].scale;
            buf[at++] = user[u].slot[k].beats;
        }
    }
    const uint32_t crc = crc32(buf, kProgFileSize - 4);
    for (int i = 0; i < 4; i++)
        buf[kProgFileSize - 4 + i] = static_cast<uint8_t>(crc >> (8 * i));
    return kProgFileSize;
}

bool deserializeProgressions(Progression* user, const uint8_t* buf, size_t length)
{
    if (buf == nullptr || length != kProgFileSize || buf[0] != 'E' || buf[1] != 'U' || buf[2] != 'P' ||
        buf[3] != 'R' || buf[4] != 1 || buf[5] != kUserProgs)
        return false;
    uint32_t crc = 0;
    for (int i = 0; i < 4; i++)
        crc |= static_cast<uint32_t>(buf[kProgFileSize - 4 + i]) << (8 * i);
    if (crc != crc32(buf, kProgFileSize - 4))
        return false;
    size_t at = 8;
    for (int u = 0; u < kUserProgs; u++)
    {
        Progression p;
        for (int k = 0; k < kProgNameLength; k++)
            p.name[k] = static_cast<char>(buf[at++]);
        p.name[kProgNameLength - 1] = 0;
        p.count = buf[at++];
        if (p.count > kProgSlots)
            p.count = 0;
        p.kept = p.count;
        for (int k = 0; k < kProgSlots; k++)
        {
            p.slot[k].root = static_cast<uint8_t>(buf[at++] % 12);
            p.slot[k].scale = buf[at] <= MAX_SCALE ? buf[at] : 2;
            at++;
            const uint8_t b = buf[at++];
            p.slot[k].beats = b < 1 ? 1 : (b > 16 ? 16 : b);
        }
        user[u] = p;
    }
    return true;
}

} // namespace euclid
