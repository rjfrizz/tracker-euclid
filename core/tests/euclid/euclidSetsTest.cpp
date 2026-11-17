// Host-side tests for set files. Not part of the firmware build.
//   c++ -std=c++17 -I core/source core/tests/euclid/euclidSetsTest.cpp \
//       core/source/euclid/euclidEngine.cpp core/source/euclid/euclidSets.cpp -o /tmp/setsTest

#include "euclid/euclidSets.h"

#include <cstdio>
#include <cstring>

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
        e.setExpression(ch, x);

        Rhythm withMode = e.getRhythm(ch);
        withMode.mode = static_cast<NoteMode>(ch % static_cast<int>(NoteMode::count));
        withMode.homeRoot = static_cast<uint8_t>((ch * 5) % 12);
        withMode.homeScale = static_cast<uint8_t>(ch * 3);
        e.setRhythm(ch, withMode);
    }
    e.setGlobalKey(Key{5, 11});
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
            ra.homeRoot != rb.homeRoot || ra.homeScale != rb.homeScale)
            return false;
    }
    Key ka = a.getGlobalKey(), kb = b.getGlobalKey();
    return ka.root == kb.root && ka.scale == kb.scale;
}

// A version 2 file: this release's layout with version 2 and bytes 35-38
// zeroed, as version 2 wrote them.
static void testLoadsVersion2()
{
    Engine a = makeEngine();
    fill(a);
    uint8_t buf[kSetFileSize];
    serializeSet(a, 2, buf, sizeof(buf));
    buf[4] = 2;
    for (int ch = 0; ch < kChannels; ch++)
        for (int i = 35; i <= 38; i++)
            buf[kSetHeaderSize + ch * kSetChannelSize + i] = 0;
    uint32_t crc = crc32(buf, kSetFileSize - 4);
    for (int i = 0; i < 4; i++)
        buf[kSetFileSize - 4 + i] = static_cast<uint8_t>(crc >> (8 * i));

    Engine b = makeEngine();
    CHECK(deserializeSet(b, buf, sizeof(buf)));
    for (int ch = 0; ch < kChannels; ch++)
    {
        CHECK(b.getExpression(ch).seqKeyMode == KeyMode::own);
        CHECK(b.getExpression(ch).transpose == 0);
        // Home key: the file's global key.
        CHECK(b.getRhythm(ch).homeRoot == 5 && b.getRhythm(ch).homeScale == 11);
        CHECK(b.getRhythm(ch).steps == a.getRhythm(ch).steps);
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
    CHECK(kSetFileSize == 340);
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
    testRejectsBadData();
    testLoadWhilePlayingWaitsForBar();
    testChangeCount();
    if (failures == 0)
        std::printf("all euclid set tests passed\n");
    return failures == 0 ? 0 : 1;
}
