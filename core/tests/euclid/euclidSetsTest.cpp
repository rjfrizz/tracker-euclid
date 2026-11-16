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
        e.setExpression(ch, x);
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
    }
    return true;
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
    CHECK(kSetFileSize == 268);

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
    testRejectsBadData();
    testLoadWhilePlayingWaitsForBar();
    testChangeCount();
    if (failures == 0)
        std::printf("all euclid set tests passed\n");
    return failures == 0 ? 0 : 1;
}
