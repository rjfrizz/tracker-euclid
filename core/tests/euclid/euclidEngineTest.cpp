// Host-side tests for the Euclidean engine. Not part of the firmware build.
//   c++ -std=c++17 -I core/source core/tests/euclid/euclidEngineTest.cpp \
//       core/source/euclid/euclidEngine.cpp -o /tmp/euclidTest && /tmp/euclidTest

#include "euclid/euclidEngine.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace euclid;

struct Event
{
    bool on;
    uint32_t tick;
    uint8_t note, channel;
};

static std::vector<Event> events;
static uint32_t now = 0;
static int failures = 0;

static void onNote(uint8_t n, uint8_t, uint8_t ch) { events.push_back({true, now, n, ch}); }
static void offNote(uint8_t n, uint8_t ch) { events.push_back({false, now, n, ch}); }
static uint8_t zeroPercent() { return 0; }
static uint8_t ninetyNine() { return 99; }
static uint8_t zeroBelow(uint8_t) { return 0; }

#define CHECK(cond)                                                                                \
    do                                                                                             \
    {                                                                                              \
        if (!(cond))                                                                               \
        {                                                                                          \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                            \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static std::string patternString(uint8_t steps, uint8_t pulses, uint8_t rot)
{
    std::string s;
    for (uint8_t i = 0; i < steps; i++)
        s += isPulse(steps, pulses, rot, i) ? 'x' : '.';
    return s;
}

static Engine makeEngine(uint8_t (*pct)() = zeroPercent)
{
    Output o;
    o.noteOn = onNote;
    o.noteOff = offNote;
    o.randomPercent = pct;
    o.randomBelow = zeroBelow;
    Engine e;
    e.init(o);
    return e;
}

static void run(Engine& e, uint32_t ticks)
{
    for (uint32_t i = 0; i < ticks; i++)
    {
        e.onMicroStep();
        now++;
    }
}

static std::vector<uint32_t> onTicks(uint8_t channel)
{
    std::vector<uint32_t> t;
    for (auto& ev : events)
        if (ev.on && ev.channel == channel)
            t.push_back(ev.tick);
    return t;
}

static void reset()
{
    events.clear();
    now = 0;
}

static void testPatterns()
{
    CHECK(patternString(8, 3, 0) == "x..x..x.");
    CHECK(patternString(16, 4, 0) == "x...x...x...x...");
    CHECK(patternString(8, 3, 1) == ".x..x..x");
    CHECK(patternString(5, 0, 0) == ".....");
    CHECK(patternString(5, 5, 0) == "xxxxx");
    for (int s = 1; s <= kMaxSteps; s++)
        for (int p = 0; p <= s; p++)
        {
            int count = 0;
            for (int i = 0; i < s; i++)
                count += isPulse(s, p, 0, i);
            CHECK(count == p);
        }
}

static void testTimingAndGate()
{
    reset();
    Engine e = makeEngine();
    Rhythm r;
    r.steps = 16;
    r.pulses = 4;
    e.setRhythm(0, r);
    Expression x;
    x.enabled = true;
    x.midiChannel = 3;
    x.gate = 50;
    e.setExpression(0, x);

    e.start();
    run(e, kMicroStepsPerBar);
    auto on = onTicks(3);
    CHECK(on.size() == 4);
    CHECK(on.size() == 4 && on[0] == 0 && on[1] == 192 && on[2] == 384 && on[3] == 576);

    // Gate 50% of a 1/16 = 24 micro-steps.
    bool sawOff = false;
    for (auto& ev : events)
        if (!ev.on && ev.channel == 3)
        {
            CHECK(ev.tick == 24 || ev.tick == 216 || ev.tick == 408 || ev.tick == 600);
            sawOff = true;
        }
    CHECK(sawOff);

    e.stop();
    int ons = 0, offs = 0;
    for (auto& ev : events)
        (ev.on ? ons : offs)++;
    CHECK(ons == offs);
}

static void testFullGateReleasesBeforeNextNote()
{
    reset();
    Engine e = makeEngine();
    Rhythm r;
    r.steps = 4;
    r.pulses = 4;
    e.setRhythm(0, r);
    Expression x;
    x.enabled = true;
    x.gate = 100;
    e.setExpression(0, x);
    e.start();
    run(e, 48 * 3);
    // on, off, on, off, on: never two note-ons in a row.
    for (size_t i = 1; i < events.size(); i++)
        CHECK(events[i].on != events[i - 1].on);
    e.stop();
}

static void testBarQueuedChange()
{
    reset();
    Engine e = makeEngine();
    Rhythm r;
    r.steps = 16;
    r.pulses = 4;
    e.setRhythm(0, r);
    e.setEnabled(0, true);
    e.start();
    run(e, 100); // mid-bar
    e.setPulses(0, 8);
    CHECK(e.hasPendingChange(0));
    CHECK(e.getActiveRhythm(0).pulses == 4);
    CHECK(e.getRhythm(0).pulses == 8);
    run(e, kMicroStepsPerBar - 100); // to the end of bar 1
    CHECK(onTicks(1).size() == 4);
    run(e, 1); // first tick of bar 2 applies the change
    CHECK(!e.hasPendingChange(0));
    run(e, kMicroStepsPerBar - 1);
    CHECK(onTicks(1).size() == 4 + 8);
    e.stop();
}

static void testStoppedChangeAppliesImmediately()
{
    reset();
    Engine e = makeEngine();
    e.setPulses(0, 7);
    CHECK(!e.hasPendingChange(0));
    CHECK(e.getActiveRhythm(0).pulses == 7);
}

static void testNoteSequenceAndMute()
{
    reset();
    Engine e = makeEngine();
    Rhythm r;
    r.steps = 4;
    r.pulses = 4;
    r.noteCount = 3;
    r.notes[0] = 60;
    r.notes[1] = 62;
    r.notes[2] = 64;
    e.setRhythm(0, r);
    e.setEnabled(0, true);
    e.start();
    run(e, 48 * 2); // 60, 62
    e.setMuted(0, true);
    run(e, 48); // 64 is skipped but the sequence still advances
    e.setMuted(0, false);
    run(e, 48); // back to 60
    std::vector<uint8_t> notes;
    for (auto& ev : events)
        if (ev.on)
            notes.push_back(ev.note);
    CHECK((notes == std::vector<uint8_t>{60, 62, 60}));
    e.stop();
}

static void testProbabilityAndDisabled()
{
    reset();
    Engine e = makeEngine(ninetyNine);
    Rhythm r;
    r.steps = 4;
    r.pulses = 4;
    e.setRhythm(0, r);
    e.setRhythm(1, r);
    Expression x;
    x.enabled = true;
    x.probability = 50;
    e.setExpression(0, x); // roll 99 >= 50: never plays
    x.midiChannel = 2;
    x.enabled = false;
    e.setExpression(1, x);
    e.start();
    run(e, kMicroStepsPerBar);
    CHECK(events.empty());
    e.stop();
}

static void testDivisions()
{
    reset();
    Engine e = makeEngine();
    Rhythm r;
    r.steps = 3;
    r.pulses = 3;
    r.division = Division::d1_8T;
    e.setRhythm(0, r);
    e.setEnabled(0, true);
    e.start();
    run(e, 192); // one beat = three 1/8 triplets
    auto on = onTicks(1);
    CHECK((on == std::vector<uint32_t>{0, 64, 128}));
    e.stop();
}

static void testNoteEditsApplyImmediately()
{
    reset();
    Engine e = makeEngine();
    Rhythm r;
    r.steps = 4;
    r.pulses = 4;
    r.noteCount = 3;
    r.notes[0] = 60;
    r.notes[1] = 62;
    r.notes[2] = 64;
    e.setRhythm(0, r);
    e.setEnabled(0, true);
    e.start();
    run(e, 48 * 2); // 60, 62; the next note would be index 2
    CHECK(e.getNextNoteIndex(0) == 2);

    // Shorten the list mid-bar: no pending change, and the index wraps.
    const uint8_t two[] = {70, 72};
    e.setNotes(0, two, 2);
    CHECK(!e.hasPendingChange(0));
    CHECK(e.getActiveRhythm(0).noteCount == 2);
    CHECK(e.getNextNoteIndex(0) == 0);
    run(e, 48 * 2); // 70, 72

    e.setDirection(0, NoteDirection::backward);
    run(e, 48 * 2); // from index 0 going backward: 70, 72

    std::vector<uint8_t> notes;
    for (auto& ev : events)
        if (ev.on)
            notes.push_back(ev.note);
    CHECK((notes == std::vector<uint8_t>{60, 62, 70, 72, 70, 72}));
    e.stop();
}

int main()
{
    testNoteEditsApplyImmediately();
    testPatterns();
    testTimingAndGate();
    testFullGateReleasesBeforeNextNote();
    testBarQueuedChange();
    testStoppedChangeAppliesImmediately();
    testNoteSequenceAndMute();
    testProbabilityAndDisabled();
    testDivisions();
    if (failures == 0)
        std::printf("all euclid engine tests passed\n");
    return failures == 0 ? 0 : 1;
}
