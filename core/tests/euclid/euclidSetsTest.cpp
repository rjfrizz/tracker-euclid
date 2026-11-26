// Host-side tests for set files. Not part of the firmware build.
//   c++ -std=c++17 -I core/source core/tests/euclid/euclidSetsTest.cpp \
//       core/source/euclid/euclidEngine.cpp core/source/euclid/euclidSets.cpp -o /tmp/setsTest

#include "euclid/euclidSets.h"

#include <cstdio>
#include <cstring>
#include <string>

using namespace euclid;

static int failures = 0;

#define CHECK(cond)                                                                                \
    do                                                                                             \
    {                                                                                              \
        if (!(cond))                                                                               \
        {                                                                                          \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                            \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static Engine makeEngine()
{
    Engine e;
    e.init(Output());
    return e;
}

static void fill(Engine& e)
{
    for (int ch = 0; ch < kChannels; ch++)
    {
        Rhythm r;
        r.steps = static_cast<uint8_t>(5 + ch * 3);
        r.pulses = static_cast<uint8_t>(2 + ch);
        r.rotation = static_cast<uint8_t>(ch);
        r.division = static_cast<Division>(ch % static_cast<int>(Division::count));
        r.direction = static_cast<NoteDirection>(ch % static_cast<int>(NoteDirection::count));
        r.noteCount = static_cast<uint8_t>(1 + ch * 2);
        for (int n = 0; n < kMaxNotes; n++)
            r.notes[n] = static_cast<uint8_t>(30 + ch * 5 + n);
        e.setRhythm(ch, r);

        Expression x;
        x.enabled = ch % 2 == 0;
        x.muted = ch % 3 == 0;
        x.midiChannel = static_cast<uint8_t>(16 - ch);
        x.velocity = static_cast<uint8_t>(40 + ch * 10);
        x.gate = static_cast<uint8_t>(10 + ch * 11);
        x.probability = static_cast<uint8_t>(100 - ch * 7);
        x.keyMode = static_cast<KeyMode>(ch % static_cast<int>(KeyMode::count));
        x.keyOffset = static_cast<uint8_t>(1 + ch);
        x.ownRoot = static_cast<uint8_t>(11 - ch);
        x.ownScale = static_cast<uint8_t>(3 + ch * 4);
        x.octave = static_cast<uint8_t>(ch % 8);
        x.range = static_cast<uint8_t>(1 + ch % 4);
        x.seqKeyMode = static_cast<KeyMode>((ch + 1) % static_cast<int>(KeyMode::count));
        x.transpose = static_cast<int8_t>(ch * 6 - 24);
        x.chord = static_cast<ChordType>(ch % static_cast<int>(ChordType::count));
        x.chordChance = static_cast<uint8_t>(30 + ch * 10);
        x.shift.on = ch % 2 == 1;
        x.shift.length = static_cast<uint8_t>(3 + ch * 3);
        x.shift.advance = static_cast<ShiftAdvance>(ch % 3);
        x.shift.units = static_cast<ShiftUnits>((ch / 2) % 2);
        for (int i = 0; i < kShiftSteps; i++)
            x.shift.step[i] = static_cast<int8_t>((i * 7 + ch) % 49 - 24);
        for (int k = 0; k < kModsPerChannel; k++)
        {
            Mod& m = x.mod[k];
            m.dest = static_cast<ModDest>((ch + k) % static_cast<int>(ModDest::count));
            m.shape = static_cast<ModShape>((ch * 2 + k) % static_cast<int>(ModShape::count));
            m.amount = static_cast<int8_t>(ch * 25 - 100 + k);
            m.rate = static_cast<ModRate>((ch + k * 3) % static_cast<int>(ModRate::count));
            m.phase = static_cast<uint8_t>(ch * 10 + k);
            m.target = static_cast<uint8_t>((k + 1) % kModsPerChannel);
            m.steps = static_cast<uint8_t>(3 + ch + k);
            m.pulses = static_cast<uint8_t>(1 + k);
            m.rotation = static_cast<uint8_t>(k);
            m.division = static_cast<Division>((ch + k) % static_cast<int>(Division::count));
            m.autoRotate = static_cast<uint8_t>(k == 0 ? 0 : 4);
            m.length = static_cast<uint8_t>(2 + ch);
            m.cc = static_cast<uint8_t>(70 + ch);
        }
        e.setExpression(ch, x);

        Rhythm withMode = e.getRhythm(ch);
        withMode.mode = static_cast<NoteMode>(ch % static_cast<int>(NoteMode::count));
        withMode.homeRoot = static_cast<uint8_t>((ch * 5) % 12);
        withMode.homeScale = static_cast<uint8_t>(ch * 3);
        withMode.tieMask = static_cast<uint16_t>((0b0110u << ch) & ((1u << withMode.noteCount) - 1) & ~1u);
        e.setRhythm(ch, withMode);
    }
    e.setGlobalKey(Key{5, 11});
    for (int ch = 0; ch < kChannels; ch++)
        e.setName(ch, static_cast<uint8_t>(ch * 4));
    e.setTextName(2, "E.PIANO 1");
    e.setTextName(5, "Fantasia");
    for (int ch = 0; ch < kChannels; ch++)
    {
        Expression x = e.getExpression(ch);
        Lane& l = x.lanes[ch % kLaneKinds];
        l.on = true;
        l.length = static_cast<uint8_t>(3 + ch);
        l.advance = static_cast<ShiftAdvance>(ch % 3);
        l.step[1] = static_cast<int8_t>(ch % 3);
        x.output = ch % 2 ? ChannelOutput::sampler : ChannelOutput::midi;
        x.instrument = static_cast<uint8_t>(ch * 5);
        e.setExpression(ch, x);
        e.toggleStep(ch, static_cast<uint8_t>(ch)); // a hand change each
    }
}

static bool sameSettings(const Engine& a, const Engine& b)
{
    for (int ch = 0; ch < kChannels; ch++)
    {
        Rhythm ra = a.getRhythm(ch), rb = b.getRhythm(ch);
        Expression xa = a.getExpression(ch), xb = b.getExpression(ch);
        if (ra.steps != rb.steps || ra.pulses != rb.pulses || ra.rotation != rb.rotation ||
            ra.division != rb.division || ra.direction != rb.direction ||
            ra.noteCount != rb.noteCount)
            return false;
        if (std::memcmp(ra.notes, rb.notes, ra.noteCount) != 0)
            return false;
        if (xa.enabled != xb.enabled || xa.muted != xb.muted || xa.midiChannel != xb.midiChannel ||
            xa.velocity != xb.velocity || xa.gate != xb.gate || xa.probability != xb.probability)
            return false;
        if (ra.mode != rb.mode || xa.keyMode != xb.keyMode || xa.keyOffset != xb.keyOffset ||
            xa.ownRoot != xb.ownRoot || xa.ownScale != xb.ownScale || xa.octave != xb.octave ||
            xa.range != xb.range || xa.seqKeyMode != xb.seqKeyMode || xa.transpose != xb.transpose ||
            ra.homeRoot != rb.homeRoot || ra.homeScale != rb.homeScale || ra.tieMask != rb.tieMask ||
            xa.chord != xb.chord || xa.chordChance != xb.chordChance || xa.output != xb.output ||
            xa.instrument != xb.instrument)
            return false;
        if (a.getName(ch) != b.getName(ch) || std::strcmp(a.getTextName(ch), b.getTextName(ch)) != 0)
            return false;
        if (ra.forceOn != rb.forceOn || ra.forceOff != rb.forceOff)
            return false;
        for (int k = 0; k < kLaneKinds; k++)
        {
            const Lane &la = xa.lanes[k], &lb = xb.lanes[k];
            if (la.on != lb.on || la.length != lb.length || la.advance != lb.advance ||
                std::memcmp(la.step, lb.step, sizeof(la.step)) != 0)
                return false;
        }
        if (xa.shift.on != xb.shift.on || xa.shift.length != xb.shift.length ||
            xa.shift.advance != xb.shift.advance || xa.shift.units != xb.shift.units)
            return false;
        for (int i = 0; i < kShiftSteps; i++)
            if (xa.shift.step[i] != xb.shift.step[i])
                return false;
        for (int k = 0; k < kModsPerChannel; k++)
        {
            const Mod &ma = xa.mod[k], &mb = xb.mod[k];
            if (ma.rate != mb.rate || ma.phase != mb.phase || ma.target != mb.target)
                return false;
            if (ma.dest != mb.dest || ma.shape != mb.shape || ma.amount != mb.amount || ma.steps != mb.steps ||
                ma.pulses != mb.pulses || ma.rotation != mb.rotation || ma.division != mb.division ||
                ma.autoRotate != mb.autoRotate || ma.length != mb.length || ma.cc != mb.cc)
                return false;
        }
    }
    for (int ch = 0; ch < kChannels; ch++)
        if (a.getName(ch) != b.getName(ch))
            return false;
    Key ka = a.getGlobalKey(), kb = b.getGlobalKey();
    return ka.root == kb.root && ka.scale == kb.scale;
}

// A version 2 file: this release's layout with version 2 and bytes 35-38
// zeroed, as version 2 wrote them.
// Converts this release's file to the 40-byte records of versions 2 and 3.
static void toVersion3Layout(const uint8_t* v4, uint8_t* v3, uint8_t version)
{
    std::memcpy(v3, v4, kSetHeaderSize);
    v3[4] = version;
    for (int ch = 0; ch < kChannels; ch++)
        std::memcpy(v3 + kSetHeaderSize + ch * kSetV3ChannelSize, v4 + kSetHeaderSize + ch * kSetChannelSize,
                    kSetV3ChannelSize);
    uint32_t crc = crc32(v3, kSetV3FileSize - 4);
    for (int i = 0; i < 4; i++)
        v3[kSetV3FileSize - 4 + i] = static_cast<uint8_t>(crc >> (8 * i));
}

// A version 10 file: the same records without the Sample lane.
static void testLoadsVersion10()
{
    Engine a = makeEngine();
    fill(a);
    CHECK(a.getExpression(4).lanes[static_cast<int>(LaneKind::sample)].on); // fill: channel 4's lane
    uint8_t v11[kSetFileSize];
    serializeSet(a, 2, v11, sizeof(v11));
    uint8_t v10[kSetV10FileSize];
    std::memcpy(v10, v11, kSetHeaderSize);
    v10[4] = 10;
    for (int ch = 0; ch < kChannels; ch++)
        std::memcpy(v10 + kSetHeaderSize + ch * kSetV10ChannelSize, v11 + kSetHeaderSize + ch * kSetChannelSize,
                    kSetV10ChannelSize);
    uint32_t crc = crc32(v10, kSetV10FileSize - 4);
    for (int i = 0; i < 4; i++)
        v10[kSetV10FileSize - 4 + i] = static_cast<uint8_t>(crc >> (8 * i));

    Engine b = makeEngine();
    CHECK(deserializeSet(b, v10, sizeof(v10)));
    CHECK(!b.getExpression(4).lanes[static_cast<int>(LaneKind::sample)].on);
    CHECK(b.getExpression(3).lanes[3].on == a.getExpression(3).lanes[3].on);
    CHECK(b.getLiveRhythm(0).forceOn == a.getLiveRhythm(0).forceOn);
}

// A version 7 file: the same records without the text names.
static void testLoadsVersion7()
{
    Engine a = makeEngine();
    fill(a);
    uint8_t v8[kSetFileSize];
    serializeSet(a, 2, v8, sizeof(v8));
    uint8_t v7[kSetV7FileSize];
    std::memcpy(v7, v8, kSetHeaderSize);
    v7[4] = 7;
    for (int ch = 0; ch < kChannels; ch++)
        std::memcpy(v7 + kSetHeaderSize + ch * kSetV7ChannelSize, v8 + kSetHeaderSize + ch * kSetChannelSize,
                    kSetV7ChannelSize);
    uint32_t crc = crc32(v7, kSetV7FileSize - 4);
    for (int i = 0; i < 4; i++)
        v7[kSetV7FileSize - 4 + i] = static_cast<uint8_t>(crc >> (8 * i));

    Engine b = makeEngine();
    b.setTextName(2, "old");
    CHECK(deserializeSet(b, v7, sizeof(v7)));
    CHECK(b.getTextName(2)[0] == 0 && b.getName(2) == a.getName(2));
    CHECK(b.getExpression(4).shift.length == a.getExpression(4).shift.length);
    CHECK(std::strcmp(a.getNameShown(2), "E.PIANO 1") == 0);
    CHECK(b.getNameShown(0) == nullptr); // name 0 is none
}

// A version 5 file, built by hand: two 12-byte modulators per channel.
static void testLoadsVersion6()
{
    Engine a = makeEngine();
    fill(a);
    uint8_t v7[kSetFileSize]; // the current version: the first 104 bytes of each record are the same
    serializeSet(a, 2, v7, sizeof(v7));
    uint8_t v6[kSetV6FileSize];
    std::memcpy(v6, v7, kSetHeaderSize);
    v6[4] = 6;
    for (int ch = 0; ch < kChannels; ch++)
        std::memcpy(v6 + kSetHeaderSize + ch * kSetV6ChannelSize, v7 + kSetHeaderSize + ch * kSetChannelSize,
                    kSetV6ChannelSize);
    uint32_t crc = crc32(v6, kSetV6FileSize - 4);
    for (int i = 0; i < 4; i++)
        v6[kSetV6FileSize - 4 + i] = static_cast<uint8_t>(crc >> (8 * i));

    Engine b = makeEngine();
    CHECK(deserializeSet(b, v6, sizeof(v6)));
    for (int ch = 0; ch < kChannels; ch++)
    {
        CHECK(!b.getExpression(ch).shift.on); // no Shift in version 6
        CHECK(b.getExpression(ch).mod[3].rate == a.getExpression(ch).mod[3].rate);
    }
}

static void testLoadsVersion5()
{
    Engine a = makeEngine();
    fill(a);
    uint8_t v6[kSetFileSize];
    serializeSet(a, 2, v6, sizeof(v6));

    uint8_t v5[kSetV5FileSize] = {};
    std::memcpy(v5, v6, kSetHeaderSize);
    v5[4] = 5;
    for (int ch = 0; ch < kChannels; ch++)
    {
        uint8_t* p = v5 + kSetHeaderSize + ch * kSetV5ChannelSize;
        std::memcpy(p, v6 + kSetHeaderSize + ch * kSetChannelSize, 48);
        uint8_t* q = p + 48;
        q[0] = static_cast<uint8_t>(ModDest::cc);
        q[1] = static_cast<uint8_t>(ModShape::triangle);
        q[2] = 128 + 40;
        q[3] = 16;
        q[4] = 4;
        q[5] = 0;
        q[6] = static_cast<uint8_t>(Division::d1_16);
        q[7] = 0;
        q[8] = 8; // 8 sixteenths: half a bar
        q[9] = 71;
    }
    uint32_t crc = crc32(v5, kSetV5FileSize - 4);
    for (int i = 0; i < 4; i++)
        v5[kSetV5FileSize - 4 + i] = static_cast<uint8_t>(crc >> (8 * i));

    Engine b = makeEngine();
    CHECK(deserializeSet(b, v5, sizeof(v5)));
    for (int ch = 0; ch < kChannels; ch++)
    {
        const Mod& m = b.getExpression(ch).mod[0];
        CHECK(m.dest == ModDest::cc && m.shape == ModShape::triangle && m.amount == 40 && m.cc == 71);
        CHECK(m.rate == ModRate::r1_2); // half a bar
        CHECK(b.getExpression(ch).mod[2].dest == ModDest::off);
        CHECK(b.getExpression(ch).chord == a.getExpression(ch).chord);
    }
}

static void testLoadsVersion4()
{
    Engine a = makeEngine();
    fill(a);
    uint8_t v5[kSetFileSize];
    serializeSet(a, 2, v5, sizeof(v5));
    uint8_t v4[kSetV4FileSize];
    std::memcpy(v4, v5, kSetHeaderSize);
    v4[4] = 4;
    for (int ch = 0; ch < kChannels; ch++)
        std::memcpy(v4 + kSetHeaderSize + ch * kSetV4ChannelSize, v5 + kSetHeaderSize + ch * kSetChannelSize,
                    kSetV4ChannelSize);
    uint32_t crc = crc32(v4, kSetV4FileSize - 4);
    for (int i = 0; i < 4; i++)
        v4[kSetV4FileSize - 4 + i] = static_cast<uint8_t>(crc >> (8 * i));

    Engine b = makeEngine();
    CHECK(deserializeSet(b, v4, sizeof(v4)));
    for (int ch = 0; ch < kChannels; ch++)
    {
        CHECK(b.getExpression(ch).chord == a.getExpression(ch).chord);
        CHECK(b.getExpression(ch).mod[0].dest == ModDest::off); // no modulators in version 4
        CHECK(b.getExpression(ch).mod[1].dest == ModDest::off);
    }
}

static void testLoadsVersion3()
{
    Engine a = makeEngine();
    fill(a);
    uint8_t v4[kSetFileSize];
    serializeSet(a, 2, v4, sizeof(v4));
    uint8_t v3[kSetV3FileSize];
    toVersion3Layout(v4, v3, 3);

    Engine b = makeEngine();
    CHECK(deserializeSet(b, v3, sizeof(v3)));
    for (int ch = 0; ch < kChannels; ch++)
    {
        CHECK(b.getName(ch) == a.getName(ch));
        CHECK(b.getExpression(ch).transpose == a.getExpression(ch).transpose);
        CHECK(b.getExpression(ch).chord == ChordType::single); // no chords in version 3
        CHECK(b.getRhythm(ch).tieMask == 0);
    }
}

// A version 2 file: the 40-byte layout with version 2 and bytes 35-39
// zeroed, as version 2 wrote them.
static void testLoadsVersion2()
{
    Engine a = makeEngine();
    fill(a);
    uint8_t v4[kSetFileSize];
    serializeSet(a, 2, v4, sizeof(v4));
    uint8_t buf[kSetV3FileSize];
    toVersion3Layout(v4, buf, 2);
    for (int ch = 0; ch < kChannels; ch++)
        for (int i = 35; i <= 39; i++)
            buf[kSetHeaderSize + ch * kSetV3ChannelSize + i] = 0;
    uint32_t crc = crc32(buf, kSetV3FileSize - 4);
    for (int i = 0; i < 4; i++)
        buf[kSetV3FileSize - 4 + i] = static_cast<uint8_t>(crc >> (8 * i));

    Engine b = makeEngine();
    CHECK(deserializeSet(b, buf, sizeof(buf)));
    for (int ch = 0; ch < kChannels; ch++)
    {
        CHECK(b.getExpression(ch).seqKeyMode == KeyMode::own);
        CHECK(b.getExpression(ch).transpose == 0);
        // Home key: the file's global key.
        CHECK(b.getRhythm(ch).homeRoot == 5 && b.getRhythm(ch).homeScale == 11);
        CHECK(b.getRhythm(ch).steps == a.getRhythm(ch).steps);
        CHECK(b.getName(ch) == 0);
    }
}

// A version 1 file, built by hand the way the first release wrote it.
static void testLoadsVersion1()
{
    uint8_t v1[kSetV1FileSize] = {};
    v1[0] = 'E';
    v1[1] = 'U';
    v1[2] = 'C';
    v1[3] = 'S';
    v1[4] = 1;
    v1[5] = kChannels;
    v1[6] = 4;
    for (int ch = 0; ch < kChannels; ch++)
    {
        uint8_t* p = v1 + kSetV1HeaderSize + ch * kSetV1ChannelSize;
        p[0] = 12;   // steps
        p[1] = 5;    // pulses
        p[2] = 1;    // rotation
        p[3] = 4;    // 1/8
        p[4] = 2;    // ping-pong
        p[5] = 2;    // notes
        p[6] = 50;
        p[7] = 53;
        p[22] = 1;   // enabled
        p[24] = static_cast<uint8_t>(ch + 1);
        p[25] = 80;
        p[26] = 30;
        p[27] = 90;
        p[28] = 0xAA; // reserved in v1: must not be read as a mode
    }
    uint32_t crc = crc32(v1, kSetV1FileSize - 4);
    v1[kSetV1FileSize - 4] = static_cast<uint8_t>(crc);
    v1[kSetV1FileSize - 3] = static_cast<uint8_t>(crc >> 8);
    v1[kSetV1FileSize - 2] = static_cast<uint8_t>(crc >> 16);
    v1[kSetV1FileSize - 1] = static_cast<uint8_t>(crc >> 24);

    Engine e = makeEngine();
    uint8_t slot = 0;
    CHECK(deserializeSet(e, v1, sizeof(v1), &slot));
    CHECK(slot == 4);
    Rhythm r = e.getRhythm(3);
    Expression x = e.getExpression(3);
    CHECK(r.steps == 12 && r.pulses == 5 && r.rotation == 1);
    CHECK(r.division == Division::d1_8 && r.direction == NoteDirection::pingPong);
    CHECK(r.noteCount == 2 && r.notes[0] == 50 && r.notes[1] == 53);
    CHECK(x.enabled && x.midiChannel == 4 && x.velocity == 80);
    // New fields keep their defaults.
    CHECK(r.mode == NoteMode::seq);
    CHECK(x.keyMode == KeyMode::follow && x.range == 2);
}

// User progressions: round the file and back; bad files are refused.
static void testProgressionsFile()
{
    Progression user[kUserProgs];
    snprintf(user[1].name, sizeof(user[1].name), "Mine");
    user[1].count = 2;
    user[1].slot[0] = ProgSlot{ 0, 1, 4 };
    user[1].slot[1] = ProgSlot{ 10, 9, 2 };
    uint8_t buf[kProgFileSize];
    CHECK(serializeProgressions(user, buf, sizeof(buf)) == kProgFileSize);
    Progression back[kUserProgs];
    CHECK(deserializeProgressions(back, buf, sizeof(buf)));
    CHECK(std::string(back[1].name) == "Mine" && back[1].count == 2 && back[0].count == 0);
    CHECK(back[1].slot[1].root == 10 && back[1].slot[1].scale == 9 && back[1].slot[1].beats == 2);
    buf[20] ^= 1;
    CHECK(!deserializeProgressions(back, buf, sizeof(buf)));
}

static void testCrcKnownValue()
{
    const char* s = "123456789";
    CHECK(crc32(reinterpret_cast<const uint8_t*>(s), 9) == 0xCBF43926u);
}

static void testRoundTrip()
{
    Engine a = makeEngine();
    fill(a);
    uint8_t buf[kSetFileSize];
    CHECK(serializeSet(a, 3, buf, sizeof(buf)) == kSetFileSize);
    CHECK(kSetFileSize == 2980);
    CHECK(kSetV10FileSize == 2420);
    CHECK(kSetV9FileSize == 2356);
    CHECK(kSetV8FileSize == 1236);
    CHECK(kSetV7FileSize == 1140);
    CHECK(kSetV6FileSize == 852);
    CHECK(kSetV5FileSize == 596);
    CHECK(kSetV4FileSize == 404);
    CHECK(kSetV3FileSize == 340);
    CHECK(kSetV1FileSize == 268);

    Engine b = makeEngine();
    uint8_t slot = 0;
    CHECK(deserializeSet(b, buf, sizeof(buf), &slot));
    CHECK(slot == 3);
    CHECK(sameSettings(a, b));
}

static void testRejectsBadData()
{
    Engine a = makeEngine();
    fill(a);
    uint8_t buf[kSetFileSize];
    serializeSet(a, 1, buf, sizeof(buf));

    Engine b = makeEngine();
    Rhythm before = b.getRhythm(0);

    uint8_t bad[kSetFileSize];
    std::memcpy(bad, buf, sizeof(bad));
    bad[20] ^= 0x40; // corrupt a byte
    CHECK(!deserializeSet(b, bad, sizeof(bad)));

    std::memcpy(bad, buf, sizeof(bad));
    bad[4] = 99; // unknown version
    CHECK(!deserializeSet(b, bad, sizeof(bad)));

    CHECK(!deserializeSet(b, buf, sizeof(buf) - 1)); // truncated
    CHECK(serializeSet(a, 1, buf, sizeof(buf) - 1) == 0); // buffer too small

    // Nothing above touched the engine.
    CHECK(b.getRhythm(0).steps == before.steps);
    CHECK(b.getRhythm(0).pulses == before.pulses);
}

static void testLoadWhilePlayingWaitsForBar()
{
    Engine a = makeEngine();
    fill(a);
    uint8_t buf[kSetFileSize];
    serializeSet(a, 0, buf, sizeof(buf));

    Engine b = makeEngine();
    b.start();
    CHECK(deserializeSet(b, buf, sizeof(buf)));
    CHECK(b.hasPendingChange(0));
    CHECK(b.getActiveRhythm(0).steps == 16); // default, until the bar
    for (int i = 0; i < kMicroStepsPerBar; i++)
        b.onMicroStep();
    CHECK(!b.hasPendingChange(0));
    CHECK(b.getActiveRhythm(0).steps == a.getRhythm(0).steps);
    b.stop();
}

static void testChangeCount()
{
    Engine e = makeEngine();
    uint32_t c0 = e.getChangeCount();
    e.setPulses(0, 3);
    uint32_t c1 = e.getChangeCount();
    CHECK(c1 > c0);
    e.setVelocity(1, 90);
    uint32_t c2 = e.getChangeCount();
    CHECK(c2 > c1);
    const uint8_t n[] = {60};
    e.setNotes(2, n, 1);
    CHECK(e.getChangeCount() > c2);
}

int main()
{
    testCrcKnownValue();
    testRoundTrip();
    testLoadsVersion1();
    testLoadsVersion2();
    testLoadsVersion3();
    testLoadsVersion4();
    testLoadsVersion5();
    testProgressionsFile();
    testLoadsVersion10();
    testLoadsVersion7();
    testLoadsVersion6();
    testRejectsBadData();
    testLoadWhilePlayingWaitsForBar();
    testChangeCount();
    if (failures == 0)
        std::printf("all euclid set tests passed\n");
    return failures == 0 ? 0 : 1;
}
