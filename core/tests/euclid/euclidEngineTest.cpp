// Host-side tests for the Euclidean engine. Not part of the firmware build.
//   c++ -std=c++17 -I core/source core/tests/euclid/euclidEngineTest.cpp \
//       core/source/euclid/euclidEngine.cpp -o /tmp/euclidTest && /tmp/euclidTest

#include "euclid/euclidEngine.h"
#include "scales.h"

#include <cstdio>
#include <string>
#include <set>
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
    e.setImmediate(true); // edits below behave as with Shift held
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
    e.setImmediate(true); // edits below behave as with Shift held
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

static std::vector<uint8_t> playedNotes()
{
    std::vector<uint8_t> notes;
    for (auto& ev : events)
        if (ev.on)
            notes.push_back(ev.note);
    return notes;
}

// Every step a pulse, so each 1/16 plays one note.
static Engine makeGeneratedEngine(NoteMode mode, uint8_t (*below)(uint8_t) = zeroBelow)
{
    Output o;
    o.noteOn = onNote;
    o.noteOff = offNote;
    o.randomPercent = zeroPercent;
    o.randomBelow = below;
    Engine e;
    e.init(o);
    Rhythm r;
    r.steps = 4;
    r.pulses = 4;
    r.mode = mode;
    e.setRhythm(0, r);
    Expression x;
    x.enabled = true;
    x.octave = 3;
    x.range = 1;
    e.setExpression(0, x);
    return e;
}

static void testScaleUpInCMajor()
{
    reset();
    Engine e = makeGeneratedEngine(NoteMode::scaleUp);
    e.setGlobalKey(Key{0, 2}); // C major
    e.start();
    run(e, 48 * 8);
    CHECK((playedNotes() == std::vector<uint8_t>{36, 38, 40, 41, 43, 45, 47, 36}));
    e.stop();
}

static void testScaleUpDownAndOffsetKey()
{
    reset();
    Engine e = makeGeneratedEngine(NoteMode::scaleUpDown);
    Expression x = e.getExpression(0);
    x.keyMode = KeyMode::offset;
    x.keyOffset = 2; // D
    e.setExpression(0, x);
    e.setGlobalKey(Key{0, 17}); // C minor pentatonic -> D minor pentatonic
    e.start();
    run(e, 48 * 9);
    // D F G A C, then back down and up again.
    CHECK((playedNotes() == std::vector<uint8_t>{38, 41, 43, 45, 48, 45, 43, 41, 38}));
    e.stop();
}

static uint8_t lcgBelow(uint8_t n)
{
    static uint32_t s = 12345;
    s = s * 1103515245u + 12345u;
    return static_cast<uint8_t>((s >> 16) % n);
}

static bool inScale(uint8_t note, uint8_t root, uint8_t scale)
{
    return (scaleDef[scale] | 1u) & (1u << ((note + 12 - root) % 12));
}

static void testRandomAndWalkStayInKeyAndRange()
{
    for (NoteMode mode : {NoteMode::random, NoteMode::walk})
    {
        reset();
        Engine e = makeGeneratedEngine(mode, lcgBelow);
        Expression x = e.getExpression(0);
        x.keyMode = KeyMode::own;
        x.ownRoot = 9;   // A
        x.ownScale = 26; // Hirajoshi, 5 notes
        x.range = 2;
        e.setExpression(0, x);
        e.start();
        run(e, 48 * 200);
        auto notes = playedNotes();
        CHECK(notes.size() == 200);
        int lastDegree = -1;
        for (uint8_t n : notes)
        {
            CHECK(inScale(n, 9, 26));
            CHECK(n >= 36 + 9 && n < 36 + 9 + 24);
            if (mode == NoteMode::walk)
            {
                // Count scale notes between the lowest note and this one.
                int degree = 0;
                for (int k = 36 + 9; k < n; k++)
                    degree += inScale(static_cast<uint8_t>(k), 9, 26);
                if (lastDegree >= 0)
                    CHECK(degree - lastDegree <= 2 && lastDegree - degree <= 2);
                lastDegree = degree;
            }
        }
        e.stop();
    }
}

static void testGlobalKeyWaitsForBar()
{
    reset();
    Engine e = makeGeneratedEngine(NoteMode::scaleUp);
    e.setGlobalKey(Key{0, 2});
    e.start();
    e.setImmediate(true); // edits below behave as with Shift held
    run(e, 100);
    e.setGlobalKey(Key{7, 2}); // G major
    CHECK(e.hasPendingGlobalKey());
    CHECK(e.getActiveGlobalKey().root == 0);
    CHECK(e.getChannelKey(0).root == 7);
    run(e, kMicroStepsPerBar - 100);
    CHECK(e.getActiveGlobalKey().root == 0);
    run(e, 1);
    CHECK(!e.hasPendingGlobalKey());
    CHECK(e.getActiveGlobalKey().root == 7);
    e.stop();
}

static Engine makeFourOnTheFloor()
{
    Engine e = makeEngine();
    Rhythm r;
    r.steps = 16;
    r.pulses = 4;
    e.setRhythm(0, r);
    Expression x;
    x.enabled = true;
    x.velocity = 100;
    e.setExpression(0, x);
    return e;
}

static void testStagedChangesWaitForCommit()
{
    reset();
    Engine e = makeFourOnTheFloor();
    e.start();
    run(e, 100);

    e.setPulses(0, 8);
    e.setVelocity(0, 40);
    CHECK(e.hasStagedChange(0));
    CHECK(e.hasAnyStagedChange());
    CHECK(!e.hasPendingChange(0));
    CHECK(e.getRhythm(0).pulses == 8);     // the screen shows the staged value
    CHECK(e.getLiveRhythm(0).pulses == 4); // nothing committed yet

    // Two bar lines pass and nothing staged is heard.
    run(e, kMicroStepsPerBar * 2 - 100);
    CHECK(onTicks(1).size() == 8);

    // Commit mid-bar: it lands on the next bar line.
    run(e, 50);
    e.commitAll();
    CHECK(!e.hasStagedChange(0));
    CHECK(e.hasPendingChange(0));
    run(e, kMicroStepsPerBar - 50); // to the end of bar 3, still 4 pulses
    CHECK(onTicks(1).size() == 12);
    run(e, kMicroStepsPerBar); // bar 4 has 8
    CHECK(onTicks(1).size() == 20);
    CHECK(!e.hasPendingChange(0));
    e.stop();
}

static uint8_t lastVelocity = 0;
static void onNoteVel(uint8_t n, uint8_t v, uint8_t ch)
{
    lastVelocity = v;
    onNote(n, v, ch);
}

static void testImmediateExpressionPlaysNow()
{
    reset();
    Output o;
    o.noteOn = onNoteVel;
    o.noteOff = offNote;
    o.randomPercent = zeroPercent;
    o.randomBelow = zeroBelow;
    Engine e;
    e.init(o);
    Rhythm r;
    r.steps = 4;
    r.pulses = 4;
    e.setRhythm(0, r);
    Expression x;
    x.enabled = true;
    x.velocity = 100;
    e.setExpression(0, x);
    e.start();
    run(e, 48); // one note at 100
    CHECK(lastVelocity == 100);

    e.setImmediate(true);
    e.setVelocity(0, 30); // Shift held: plays from the next note, not the bar
    e.setImmediate(false);
    CHECK(!e.hasStagedChange(0));
    CHECK(!e.hasPendingChange(0));
    run(e, 48);
    CHECK(lastVelocity == 30);

    // Key settings committed immediately still wait for the launch point.
    e.setImmediate(true);
    Expression k = e.getExpression(0);
    k.octave = 5;
    e.setExpression(0, k);
    e.setImmediate(false);
    CHECK(e.hasPendingChange(0));
    CHECK(e.getLiveExpression(0).octave == 5);
    e.stop();
}

static void testCommitOneChannel()
{
    reset();
    Engine e = makeFourOnTheFloor();
    Rhythm r = e.getRhythm(0);
    e.setRhythm(1, r);
    Expression x = e.getExpression(0);
    x.midiChannel = 2;
    e.setExpression(1, x);
    e.start();
    e.setPulses(0, 2);
    e.setPulses(1, 3);
    e.commit(1);
    CHECK(e.hasStagedChange(0));
    CHECK(!e.hasStagedChange(1));
    CHECK(e.hasPendingChange(1));
    run(e, kMicroStepsPerBar + 1);
    CHECK(e.getActiveRhythm(1).pulses == 3);
    CHECK(e.getActiveRhythm(0).pulses == 4);
    e.stop();
}

static void testLaunchBeat()
{
    reset();
    Engine e = makeFourOnTheFloor();
    e.setLaunch(Launch::beat);
    e.start();
    run(e, 10);
    e.setPulses(0, 16);
    e.commitAll();
    run(e, 192 - 10); // to the end of beat 1
    CHECK(e.hasPendingChange(0));
    run(e, 1); // beat 2 starts
    CHECK(!e.hasPendingChange(0));
    CHECK(e.getActiveRhythm(0).pulses == 16);
    e.stop();
}

static void testMuteIsNeverStaged()
{
    reset();
    Engine e = makeFourOnTheFloor();
    e.start();
    e.setVelocity(0, 50); // something staged
    e.setMuted(0, true);
    CHECK(e.getLiveExpression(0).muted);
    run(e, kMicroStepsPerBar);
    CHECK(onTicks(1).empty()); // muted at once
    e.commitAll();             // committing doesn't undo the mute
    run(e, kMicroStepsPerBar * 2);
    CHECK(onTicks(1).empty());
    CHECK(e.getExpression(0).muted);
    e.stop();
}

static void testStagedGlobalKey()
{
    reset();
    Engine e = makeGeneratedEngine(NoteMode::scaleUp);
    e.setGlobalKey(Key{0, 2});
    e.start();
    e.setGlobalKey(Key{5, 2}); // F major, staged
    CHECK(e.hasStagedGlobalKey());
    CHECK(!e.hasPendingGlobalKey());
    run(e, kMicroStepsPerBar * 2);
    CHECK(e.getActiveGlobalKey().root == 0);
    e.commitAll();
    CHECK(e.hasPendingGlobalKey());
    CHECK(!e.hasStagedGlobalKey());
    run(e, kMicroStepsPerBar + 1);
    CHECK(e.getActiveGlobalKey().root == 5);
    e.stop();
}

// A SEQ channel with every step a pulse and the given list, entered while
// the global key is `home`.
static Engine makeSeqEngine(Key home, std::vector<uint8_t> notes)
{
    Engine e = makeEngine();
    e.setGlobalKey(home);
    Rhythm r;
    r.steps = 4;
    r.pulses = 4;
    e.setRhythm(0, r);
    e.setNotes(0, notes.data(), static_cast<uint8_t>(notes.size()));
    e.setEnabled(0, true);
    return e;
}

static std::vector<uint8_t> playSeq(Engine& e, int pulses)
{
    reset();
    e.start();
    run(e, 48 * pulses);
    e.stop();
    return playedNotes();
}

static void testSeqOwnIgnoresGlobalKey()
{
    CHECK(Expression().seqKeyMode == KeyMode::follow); // new lists follow the master key
    Engine e = makeSeqEngine(Key{0, 1}, {36, 39, 43});
    Expression x = e.getExpression(0);
    x.seqKeyMode = KeyMode::own;
    e.setExpression(0, x);
    e.setGlobalKey(Key{5, 2}); // F major: an Own list doesn't care
    CHECK((playSeq(e, 3) == std::vector<uint8_t>{36, 39, 43}));
    CHECK(e.getRhythm(0).homeRoot == 0 && e.getRhythm(0).homeScale == 1);
}

static void testSeqFollowMovesWithTheRoot()
{
    Engine e = makeSeqEngine(Key{0, 1}, {36, 39, 43}); // C Eb G in C minor
    Expression x = e.getExpression(0);
    x.seqKeyMode = KeyMode::follow;
    e.setExpression(0, x);
    e.setGlobalKey(Key{5, 1}); // F minor: up a fourth
    CHECK((playSeq(e, 3) == std::vector<uint8_t>{41, 44, 48}));
    e.setGlobalKey(Key{7, 1}); // G minor: the short way round is down a fourth
    CHECK((playSeq(e, 3) == std::vector<uint8_t>{31, 34, 38}));
}

static void testSeqFollowSnapsToTheScale()
{
    Engine e = makeSeqEngine(Key{0, 2}, {36, 40, 43}); // C E G in C major
    Expression x = e.getExpression(0);
    x.seqKeyMode = KeyMode::follow;
    e.setExpression(0, x);
    e.setGlobalKey(Key{0, 1}); // C minor: E has no place, the lower neighbour Eb wins
    CHECK((playSeq(e, 3) == std::vector<uint8_t>{36, 39, 43}));
}

static void testSeqOffsetAndTranspose()
{
    Engine e = makeSeqEngine(Key{0, 1}, {36});
    Expression x = e.getExpression(0);
    x.seqKeyMode = KeyMode::offset;
    x.keyOffset = 3; // Eb minor
    e.setExpression(0, x);
    CHECK((playSeq(e, 1) == std::vector<uint8_t>{39}));

    x.seqKeyMode = KeyMode::own;
    x.transpose = -5;
    e.setExpression(0, x);
    CHECK((playSeq(e, 1) == std::vector<uint8_t>{31}));
}

static void testTransposeGeneratedMode()
{
    reset();
    Engine e = makeGeneratedEngine(NoteMode::scaleUp);
    e.setGlobalKey(Key{0, 2});
    Expression x = e.getExpression(0);
    x.transpose = 12;
    e.setExpression(0, x);
    e.start();
    run(e, 48 * 3);
    CHECK((playedNotes() == std::vector<uint8_t>{48, 50, 52}));
    e.stop();
}

static Engine makeChordEngine(NoteMode mode, ChordType chord, uint8_t range, Key key)
{
    Engine e = makeGeneratedEngine(mode);
    e.setGlobalKey(key);
    Expression x = e.getExpression(0);
    x.chord = chord;
    x.range = range;
    e.setExpression(0, x);
    return e;
}

static std::vector<uint8_t> playFor(Engine& e, int pulses)
{
    reset();
    e.start();
    run(e, 48 * pulses);
    e.stop();
    return playedNotes();
}

static void testScaleUpWithTriads()
{
    Engine e = makeChordEngine(NoteMode::scaleUp, ChordType::triad, 1, Key{0, 2});
    CHECK((playFor(e, 2) == std::vector<uint8_t>{36, 40, 43, 38, 41, 45})); // C E G, then D F A

    int ons = 0, offs = 0;
    for (auto& ev : events)
        (ev.on ? ons : offs)++;
    CHECK(ons == offs); // every chord note released
}

static void testArps()
{
    Engine up = makeChordEngine(NoteMode::arpUp, ChordType::single, 2, Key{0, 1}); // single = triad
    CHECK((playFor(up, 7) == std::vector<uint8_t>{36, 39, 43, 48, 51, 55, 36}));

    Engine power = makeChordEngine(NoteMode::arpUpDown, ChordType::power, 1, Key{0, 1});
    CHECK((playFor(power, 4) == std::vector<uint8_t>{36, 43, 36, 43}));

    Engine ninth = makeChordEngine(NoteMode::arpUp, ChordType::ninth, 1, Key{0, 2});
    CHECK((playFor(ninth, 6) == std::vector<uint8_t>{36, 40, 43, 47, 50, 36}));
}

static void testChordChance()
{
    Engine e = makeChordEngine(NoteMode::scaleUp, ChordType::triad, 1, Key{0, 2});
    Expression x = e.getExpression(0);
    x.chordChance = 0; // the stub's roll is 0, so only 0% skips the chord
    e.setExpression(0, x);
    CHECK((playFor(e, 2) == std::vector<uint8_t>{36, 38}));
}

static void testSeqChordEntries()
{
    // C E G tied into one entry, then a single D.
    Engine e = makeSeqEngine(Key{0, 2}, {36});
    const uint8_t notes[] = {36, 40, 43, 50};
    e.setNotes(0, notes, 4, (1u << 1) | (1u << 2));
    Rhythm r = e.getRhythm(0);
    CHECK(entryCount(r) == 2);
    CHECK(entryStart(r, 1) == 3 && entryLength(r, 0) == 3 && entryLength(r, 1) == 1);
    CHECK((playSeq(e, 3) == std::vector<uint8_t>{36, 40, 43, 50, 36, 40, 43}));

    // The Chord setting stacks on single entries only.
    Expression x = e.getExpression(0);
    x.chord = ChordType::seventh;
    e.setExpression(0, x);
    CHECK((playSeq(e, 2) == std::vector<uint8_t>{36, 40, 43, 50, 53, 57, 60})); // D F A C
}

struct CcEvent
{
    uint8_t cc, value, channel;
};
static std::vector<CcEvent> ccs;
static std::vector<uint8_t> velocitiesHeard;
static void onNoteRecord(uint8_t n, uint8_t v, uint8_t ch)
{
    velocitiesHeard.push_back(v);
    onNote(n, v, ch);
}
static void onCc(uint8_t cc, uint8_t value, uint8_t ch) { ccs.push_back({cc, value, ch}); }

static Engine makeModEngine(Mod m, uint8_t velocity = 60)
{
    Output o;
    o.noteOn = onNoteRecord;
    o.noteOff = offNote;
    o.controlChange = onCc;
    o.randomPercent = zeroPercent;
    o.randomBelow = zeroBelow;
    Engine e;
    e.init(o);
    Rhythm r;
    r.steps = 4;
    r.pulses = 4;
    e.setRhythm(0, r);
    Expression x;
    x.enabled = true;
    x.velocity = velocity;
    x.mod[0] = m;
    e.setExpression(0, x);
    return e;
}

static void testAccentOnVelocity()
{
    reset();
    velocitiesHeard.clear();
    Mod m;
    m.dest = ModDest::velocity;
    m.shape = ModShape::euclidAccent;
    m.amount = 50; // +63
    m.steps = 4;
    m.pulses = 1;
    Engine e = makeModEngine(m);
    e.start();
    run(e, 48 * 8);
    e.stop();
    CHECK((velocitiesHeard == std::vector<uint8_t>{123, 60, 60, 60, 123, 60, 60, 60}));
}

static void testAccentOnPitchIsAnOctave()
{
    reset();
    Mod m;
    m.dest = ModDest::pitch;
    m.shape = ModShape::euclidAccent;
    m.amount = 50; // +12 semitones
    m.steps = 2;
    m.pulses = 1;
    Engine e = makeModEngine(m);
    e.start();
    run(e, 48 * 4);
    e.stop();
    CHECK((playedNotes() == std::vector<uint8_t>{72, 60, 72, 60}));
}

static void testAutoRotate()
{
    Mod m;
    m.dest = ModDest::velocity;
    m.steps = 4;
    m.pulses = 1;
    m.shape = ModShape::euclidAccent;
    m.autoRotate = 1; // one step later every bar
    Engine e = makeModEngine(m);
    e.start();
    CHECK(e.getModRotation(0, 0) == 0);
    run(e, kMicroStepsPerBar);
    CHECK(e.getModRotation(0, 0) == 1);
    run(e, kMicroStepsPerBar * 3);
    CHECK(e.getModRotation(0, 0) == 0); // wrapped after 4 bars
    e.stop();
}

static void testEnvelopeDecays()
{
    Mod m;
    m.dest = ModDest::gate;
    m.shape = ModShape::euclidEnvelope;
    m.steps = 4;
    m.pulses = 1;
    m.length = 2; // falls to nothing over two 1/16 steps
    Engine e = makeModEngine(m);
    e.start();
    // getModValue() is the value worked out for the micro-step just played.
    run(e, 1); // on the pulse
    int16_t a = e.getModValue(0, 0);
    run(e, 48);
    int16_t b = e.getModValue(0, 0); // one step later: half way
    run(e, 48);
    int16_t c = e.getModValue(0, 0); // two steps later: gone
    e.stop();
    CHECK(a > 980);
    CHECK(b == 500);
    CHECK(c == 0);
}

static void testCcSquareLfo()
{
    reset();
    ccs.clear();
    Mod m;
    m.dest = ModDest::cc;
    m.shape = ModShape::square;
    m.amount = 100;
    m.rate = ModRate::r1_4; // one cycle a beat
    m.cc = 74;
    Engine e = makeModEngine(m);
    Expression x = e.getExpression(0);
    x.midiChannel = 5;
    e.setExpression(0, x);
    e.start();
    run(e, 192 * 2); // two periods
    e.stop();
    // Only changes are sent: high, low, high, low, swinging around 64.
    CHECK(ccs.size() == 4);
    CHECK(ccs.size() == 4 && ccs[0].value == 127 && ccs[1].value == 1 && ccs[0].cc == 74 && ccs[0].channel == 5);
}

static void testSineIsBipolarAndBarLocked()
{
    Mod m;
    m.dest = ModDest::velocity;
    m.shape = ModShape::sine;
    m.rate = ModRate::bar1;
    Engine e = makeModEngine(m);
    e.start();
    run(e, kMicroStepsPerBar / 4 + 1); // a quarter of the way: the top
    int16_t top = e.getModValue(0, 0);
    run(e, kMicroStepsPerBar / 2); // three quarters: the bottom
    int16_t bottom = e.getModValue(0, 0);
    run(e, kMicroStepsPerBar / 4); // a bar later: back to the start
    int16_t again = e.getModValue(0, 0);
    e.stop();
    CHECK(top > 990);
    CHECK(bottom < -990);
    CHECK(again > -20 && again < 20);
}

static void testPhaseOffset()
{
    Mod m;
    m.dest = ModDest::velocity;
    m.shape = ModShape::sawUp;
    m.rate = ModRate::bar1;
    m.phase = 50; // half a cycle on
    Engine e = makeModEngine(m);
    e.start();
    run(e, 1);
    int16_t v = e.getModValue(0, 0);
    e.stop();
    CHECK(v > -20 && v < 20); // a saw from -1 to 1, half way: 0
}

static void testModAmountScalesAnother()
{
    // Mod A: a full square on velocity. Mod B: a square on Mod A's depth.
    Mod a;
    a.dest = ModDest::velocity;
    a.shape = ModShape::euclidAccent;
    a.amount = 20; // +25 velocity on pulses
    a.steps = 1;
    a.pulses = 1; // every step
    Engine e = makeModEngine(a);
    Expression x = e.getExpression(0);
    Mod& b = x.mod[1];
    b.dest = ModDest::modAmount;
    b.target = 0;
    b.shape = ModShape::square;
    b.amount = 100;
    b.rate = ModRate::bar1; // first half of the bar +1 (depth x2), second half -1 (depth 0)
    e.setExpression(0, x);

    reset();
    velocitiesHeard.clear();
    e.start();
    run(e, kMicroStepsPerBar);
    e.stop();
    // A note every 1/16: 16 in the bar.
    CHECK(velocitiesHeard.size() == 16);
    CHECK(velocitiesHeard.size() == 16 && velocitiesHeard[1] == 60 + 50); // depth doubled
    CHECK(velocitiesHeard.size() == 16 && velocitiesHeard[12] == 60);     // depth 0
}

static void testModRateSpeedsAnotherUp()
{
    // Mod A: a 1-bar saw. Mod B, steady at +1, on Mod A's rate: twice as fast.
    Mod a;
    a.dest = ModDest::velocity;
    a.shape = ModShape::sawUp;
    a.rate = ModRate::bar1;
    Engine e = makeModEngine(a);
    Expression x = e.getExpression(0);
    Mod& b = x.mod[1];
    b.dest = ModDest::modRate;
    b.target = 0;
    b.shape = ModShape::euclidAccent; // always on: 1
    b.steps = 1;
    b.pulses = 1;
    b.amount = 100;
    e.setExpression(0, x);
    e.start();
    run(e, kMicroStepsPerBar / 2 + 2);
    int16_t v = e.getModValue(0, 0); // half a bar at double speed: a whole cycle
    e.stop();
    CHECK(v < -950); // back at the start of the saw
}

static void testTwoModulatorsOnOneCcAddUp()
{
    reset();
    ccs.clear();
    Mod a;
    a.dest = ModDest::cc;
    a.shape = ModShape::square;
    a.rate = ModRate::bar1;
    a.amount = 50; // +-31
    a.cc = 20;
    Engine e = makeModEngine(a);
    Expression x = e.getExpression(0);
    x.mod[1] = a; // the same again: together +-63
    e.setExpression(0, x);
    e.start();
    run(e, 24 * 2);
    e.stop();
    CHECK(!ccs.empty() && ccs[0].cc == 20 && ccs[0].value == 64 + 62);
    CHECK(ccs.size() == 1); // one stream for the CC, not one per modulator
}

static void testMuteAtLaunch()
{
    reset();
    Engine e = makeFourOnTheFloor();
    e.start();
    run(e, 100);
    e.setMutedAtLaunch(0, true);
    CHECK(!e.getLiveExpression(0).muted); // not yet
    e.commitAll();                         // a commit with nothing else staged keeps it
    CHECK(e.hasPendingChange(0));
    run(e, kMicroStepsPerBar - 100);
    CHECK(!e.getLiveExpression(0).muted);
    run(e, 1); // the bar line
    CHECK(e.getLiveExpression(0).muted);
    size_t heard = onTicks(1).size();
    run(e, kMicroStepsPerBar);
    CHECK(onTicks(1).size() == heard); // silent from the bar
    e.stop();
}

static void testShiftPerStepDriftsAgainstTheRhythm()
{
    // A 3-step lane (0, +12, -12) on a 4-step rhythm with every step a pulse.
    Engine e = makeSeqEngine(Key{0, 1}, {60});
    Expression x = e.getExpression(0);
    x.shift.on = true;
    x.shift.length = 3;
    x.shift.step[1] = 12;
    x.shift.step[2] = -12;
    e.setExpression(0, x);
    CHECK((playSeq(e, 6) == std::vector<uint8_t>{60, 72, 48, 60, 72, 48}));
}

static void testShiftPerPulseSkipsRests()
{
    // Pulses on steps 0 and 2 of 4: per pulse, each note takes the next value.
    Engine e = makeSeqEngine(Key{0, 1}, {60});
    Rhythm r = e.getRhythm(0);
    r.pulses = 2;
    e.setRhythm(0, r);
    Expression x = e.getExpression(0);
    x.shift.on = true;
    x.shift.length = 2;
    x.shift.advance = ShiftAdvance::perPulse;
    x.shift.step[1] = 7;
    e.setExpression(0, x);
    CHECK((playSeq(e, 8) == std::vector<uint8_t>{60, 67, 60, 67}));

    // Per step, the rests use up lane steps: always step 0 here.
    x.shift.advance = ShiftAdvance::perStep;
    e.setExpression(0, x);
    CHECK((playSeq(e, 8) == std::vector<uint8_t>{60, 60, 60, 60}));
}

static void testShiftPerCycleHoldsForTheCycle()
{
    // A 2-step lane (0, +12) on a 4-step rhythm: a whole cycle each.
    Engine e = makeSeqEngine(Key{0, 1}, {60});
    Expression x = e.getExpression(0);
    x.shift.on = true;
    x.shift.length = 2;
    x.shift.advance = ShiftAdvance::perCycle;
    x.shift.step[1] = 12;
    e.setExpression(0, x);
    CHECK((playSeq(e, 12) == std::vector<uint8_t>{60, 60, 60, 60, 72, 72, 72, 72, 60, 60, 60, 60}));

    // Rests don't matter: pulses on steps 0 and 2 still change by cycle.
    Rhythm r = e.getRhythm(0);
    r.pulses = 2;
    e.setRhythm(0, r);
    CHECK((playSeq(e, 12) == std::vector<uint8_t>{60, 60, 72, 72, 60, 60}));

    // It repeats after length cycles: 2 cycles of 4 sixteenths.
    CHECK(e.repeatLength(0) == 2u * 4 * 48);
}

static void testChordLanePerPulse()
{
    // C major, a lone C: every other note a triad.
    Engine e = makeSeqEngine(Key{0, 2}, {60});
    Expression x = e.getExpression(0);
    Lane& l = x.lanes[static_cast<int>(LaneKind::chord)];
    l.on = true;
    l.length = 2;
    l.advance = ShiftAdvance::perPulse;
    l.step[1] = static_cast<int8_t>(ChordType::triad);
    e.setExpression(0, x);
    CHECK((playSeq(e, 4) == std::vector<uint8_t>{60, 60, 64, 67, 60, 60, 64, 67}));
    CHECK(e.getExpression(0).chord == ChordType::single); // the channel's own chord is untouched
}

static void testDivisionLanePerCycle()
{
    // Four 1/16 pulses, then a cycle of 1/8s, then 1/16s again.
    Engine e = makeSeqEngine(Key{0, 2}, {60});
    Expression x = e.getExpression(0);
    Lane& l = x.lanes[static_cast<int>(LaneKind::division)];
    l.on = true;
    l.length = 2;
    l.advance = ShiftAdvance::perStep; // division lanes go per cycle whatever this says
    l.step[1] = static_cast<int8_t>(Division::d1_8);
    e.setExpression(0, x);
    reset();
    e.start();
    run(e, 48 * 4 + 96 * 4 + 48 * 2);
    e.stop();
    CHECK((onTicks(1) == std::vector<uint32_t>{0, 48, 96, 144, 192, 288, 384, 480, 576, 624}));
    CHECK(e.repeatLength(0) == 0); // a division lane: no repeat worked out
}

static void testModeLaneAndNone()
{
    // Scale up with a lane that does nothing plays as without it...
    Engine plain = makeGeneratedEngine(NoteMode::scaleUp);
    plain.setGlobalKey(Key{0, 2});
    reset();
    plain.start();
    run(plain, 48 * 8);
    const std::vector<uint8_t> want = playedNotes();

    Engine e = makeGeneratedEngine(NoteMode::scaleUp);
    e.setGlobalKey(Key{0, 2});
    Expression x = e.getExpression(0);
    x.lanes[static_cast<int>(LaneKind::mode)].on = true; // all steps "-"
    e.setExpression(0, x);
    reset();
    e.start();
    run(e, 48 * 8);
    CHECK(playedNotes() == want);

    // ...and a SEQ step plays the list's note in its place.
    e.stop();
    e.setNotes(0, std::vector<uint8_t>{72}.data(), 1);
    x = e.getExpression(0);
    Lane& l = x.lanes[static_cast<int>(LaneKind::mode)];
    l.length = 2;
    l.advance = ShiftAdvance::perPulse;
    l.step[1] = static_cast<int8_t>(NoteMode::seq);
    e.setExpression(0, x);
    reset();
    e.start();
    run(e, 48 * 4);
    std::vector<uint8_t> got = playedNotes();
    CHECK(got.size() == 4 && got[1] == 72 && got[3] == 72 && got[0] != 72);
    CHECK(e.getRhythm(0).mode == NoteMode::scaleUp);
    CHECK(laneValueName(LaneKind::mode, static_cast<int8_t>(NoteMode::seq))[0] == 'S');
    CHECK(laneValueName(LaneKind::chord, kLaneNone)[0] == '-');
}

static std::vector<std::pair<uint32_t, int>> sampleEvents; // (tick, note or -1 for off)
static uint8_t sampleInstrument = 255;
static void onSample(uint8_t ch, uint8_t instrument, uint8_t note, uint8_t)
{
    if (ch == 0)
    {
        sampleEvents.push_back({ now, note });
        sampleInstrument = instrument;
    }
}
static void offSample(uint8_t ch)
{
    if (ch == 0)
        sampleEvents.push_back({ now, -1 });
}

static void testSamplerOutput()
{
    // A triad on the sampler: its first note only, on instrument 5, no MIDI.
    Engine e = makeSeqEngine(Key{0, 2}, {60});
    Output o;
    o.noteOn = onNote;
    o.noteOff = offNote;
    o.sampleOn = onSample;
    o.sampleOff = offSample;
    o.randomPercent = zeroPercent;
    e.init(o);
    e.setGlobalKey(Key{0, 2});
    Rhythm r;
    r.steps = 4;
    r.pulses = 4;
    e.setRhythm(0, r);
    uint8_t n = 60;
    e.setNotes(0, &n, 1);
    e.setEnabled(0, true);
    Expression x = e.getExpression(0);
    x.chord = ChordType::triad;
    x.output = ChannelOutput::sampler;
    x.instrument = 5;
    x.gate = 50;
    x.mod[0].dest = ModDest::cc; // CCs only go to MIDI
    x.mod[0].amount = 100;
    x.mod[0].shape = ModShape::sine;
    e.setExpression(0, x);
    reset();
    sampleEvents.clear();
    e.start();
    run(e, 48 * 2);
    e.stop();
    CHECK(events.empty());
    CHECK(sampleInstrument == 5);
    CHECK(sampleEvents.size() >= 4);
    CHECK(sampleEvents[0].first == 0 && sampleEvents[0].second == 60);
    CHECK(sampleEvents[1].first == 24 && sampleEvents[1].second == -1); // half a 1/16
    CHECK(sampleEvents[2].first == 48 && sampleEvents[2].second == 60);
}

// The Sample lane: each pulse its own instrument (a step at "-" keeps the
// channel's), the note still the sequence's.
static std::vector<uint8_t> samplesHeard;
static void onSampleRecord(uint8_t ch, uint8_t instrument, uint8_t, uint8_t)
{
    if (ch == 0)
        samplesHeard.push_back(instrument);
}
static void testSampleLane()
{
    Engine e = makeSeqEngine(Key{0, 2}, {60});
    Output o;
    o.sampleOn = onSampleRecord;
    o.randomPercent = zeroPercent;
    e.init(o);
    Rhythm r;
    r.steps = 4;
    r.pulses = 4;
    e.setRhythm(0, r);
    uint8_t n = 60;
    e.setNotes(0, &n, 1);
    e.setEnabled(0, true);
    Expression x = e.getExpression(0);
    x.output = ChannelOutput::sampler;
    x.instrument = 5;
    Lane& l = x.lanes[static_cast<int>(LaneKind::sample)];
    l.on = true;
    l.length = 3;
    l.advance = ShiftAdvance::perPulse;
    l.step[0] = 0;
    l.step[1] = kLaneNone;
    l.step[2] = 11;
    e.setExpression(0, x);
    samplesHeard.clear();
    e.start();
    run(e, 48 * 6);
    e.stop();
    CHECK((samplesHeard == std::vector<uint8_t>{ 0, 5, 11, 0, 5, 11 }));
    CHECK(e.getExpression(0).instrument == 5);
    CHECK(laneValueCount(LaneKind::sample) == kSampleInstruments);
    CHECK(std::string(laneValueName(LaneKind::sample, 11)) == "12");
}

// The Pulses lane: each cycle its own number of pulses (a step at "-" the
// rhythm's), the Euclidean spread worked out afresh.
static void testPulsesLane()
{
    Engine e = makeSeqEngine(Key{0, 2}, {60});
    Rhythm r;
    r.steps = 8;
    r.pulses = 2;
    e.setRhythm(0, r);
    uint8_t n = 60;
    e.setNotes(0, &n, 1);
    e.setEnabled(0, true);
    Expression x = e.getExpression(0);
    Lane& l = x.lanes[static_cast<int>(LaneKind::pulses)];
    l.on = true;
    l.length = 3;
    l.advance = ShiftAdvance::perPulse; // made per cycle
    l.step[0] = 4;
    l.step[1] = kLaneNone;
    l.step[2] = 40; // out of range: none
    e.setExpression(0, x);
    CHECK(e.getExpression(0).lanes[static_cast<int>(LaneKind::pulses)].advance == ShiftAdvance::perCycle);
    CHECK(e.getExpression(0).lanes[static_cast<int>(LaneKind::pulses)].step[2] == kLaneNone);
    x = e.getExpression(0);
    x.lanes[static_cast<int>(LaneKind::pulses)].step[2] = 20; // more than the steps: all of them
    e.setExpression(0, x);
    reset();
    e.start();
    std::vector<int> perCycle;
    for (int cycle = 0; cycle < 3; cycle++)
    {
        const size_t before = events.size();
        run(e, 48 * 8);
        int on = 0;
        for (size_t i = before; i < events.size(); i++)
            on += events[i].on;
        perCycle.push_back(on);
        if (cycle == 0)
            CHECK(e.getCyclePulses(0) == 4);
    }
    e.stop();
    CHECK((perCycle == std::vector<int>{ 4, 2, 8 }));
    CHECK(std::string(laneValueName(LaneKind::pulses, 12)) == "12");
}

// The Perform page's Walk, Arp and Scale: every channel in that mode for
// now, starting near its last note (a SEQ channel in its own octave), then
// back to its own.
static void testModeOverride()
{
    Engine e = makeSeqEngine(Key{0, 2}, {72, 74}); // C major, around C5
    reset();
    e.start();
    run(e, 48 * 2); // 72 74
    e.setModeOverride(static_cast<int8_t>(NoteMode::arpUp));
    run(e, 48 * 4); // the triad from near 74 in that octave (C5, a tie with E5), up
    e.setModeOverride(-1);
    run(e, 48 * 2); // the sequence where it was: 72 74
    e.stop();
    CHECK((playedNotes() == std::vector<uint8_t>{ 72, 74, 72, 76, 79, 84, 72, 74 }));
    CHECK(e.getRhythm(0).mode == NoteMode::seq);
    // Scale up from near the last note.
    Engine s = makeSeqEngine(Key{0, 2}, {64});
    reset();
    s.start();
    run(s, 48);
    s.setModeOverride(static_cast<int8_t>(NoteMode::scaleUp));
    run(s, 48 * 3);
    s.stop();
    CHECK((playedNotes() == std::vector<uint8_t>{ 64, 64, 65, 67 }));
    s.setModeOverride(99);
    CHECK(s.getModeOverride() == -1);
}

// The Perform page's Chord: a chord on every note, turned over by the wheel.
static void testChordOverride()
{
    Engine e = makeSeqEngine(Key{0, 2}, {60});
    e.setChordOverride(static_cast<int8_t>(ChordType::triad));
    auto chordHeard = [&](int8_t inversion) {
        e.setChordInversion(inversion);
        reset();
        e.start();
        run(e, 10);
        e.stop();
        std::set<uint8_t> s;
        for (auto& ev : events)
            if (ev.on)
                s.insert(ev.note);
        return s;
    };
    CHECK(chordHeard(0) == std::set<uint8_t>({ 60, 64, 67 }));
    CHECK(chordHeard(1) == std::set<uint8_t>({ 64, 67, 72 }));
    CHECK(chordHeard(2) == std::set<uint8_t>({ 67, 72, 76 }));
    CHECK(chordHeard(-1) == std::set<uint8_t>({ 55, 60, 64 }));
    e.setChordOverride(-1);
    CHECK(chordHeard(1) == std::set<uint8_t>({ 60 })); // off: the note as it was
    CHECK(e.getExpression(0).chord == ChordType::single);
}

// The Perform page's Pulses: more pulses on every channel, up to its steps.
static void testPulseBoost()
{
    Engine e = makeSeqEngine(Key{0, 2}, {60});
    Rhythm r;
    r.steps = 8;
    r.pulses = 2;
    e.setRhythm(0, r);
    auto pulsesIn = [&](uint8_t boost) {
        e.setPulseBoost(boost);
        reset();
        e.start();
        run(e, 48 * 8);
        e.stop();
        return playedNotes().size();
    };
    CHECK(pulsesIn(0) == 2);
    CHECK(pulsesIn(3) == 5);
    CHECK(pulsesIn(20) == 8);
}

// The Perform page's Jog: channels off the clock, moved by the wheel, then
// back in time (snap) or carried on from where they got to.
static void testJog()
{
    Engine e = makeEngine();
    e.setGlobalKey(Key{0, 2});
    Rhythm r;
    r.steps = 4;
    r.pulses = 4;
    for (int ch = 0; ch < 2; ch++)
    {
        e.setRhythm(ch, r);
        Expression x = e.getExpression(ch);
        x.enabled = true;
        x.midiChannel = static_cast<uint8_t>(ch + 1);
        e.setExpression(ch, x);
    }
    const uint8_t n = 60;
    e.setNotes(0, &n, 1);
    e.setNotes(1, &n, 1);
    e.setJogMask(0x01);
    reset();
    e.start();
    run(e, 10);
    CHECK(e.startJog() && e.isChannelJogged(0) && !e.isChannelJogged(1));
    auto onsOf = [&](uint8_t midi) {
        int k = 0;
        for (auto& ev : events)
            k += ev.on && ev.channel == midi;
        return k;
    };
    reset();
    run(e, 48 * 4); // the clock: channel 2's four, channel 1 none
    CHECK(onsOf(1) == 0 && onsOf(2) == 4);
    e.jog(48 * 2); // the wheel: two of channel 1's steps
    CHECK(onsOf(1) == 2);
    e.stopJog(true); // back where the clock has it: with channel 2
    reset();
    run(e, 48);
    bool together = false;
    for (auto& a : events)
        for (auto& b : events)
            together |= a.on && b.on && a.channel == 1 && b.channel == 2 && a.tick == b.tick;
    CHECK(together);
    // Carried on instead: out of time with channel 2, by the jogged half step.
    CHECK(e.startJog());
    e.jog(24);
    e.stopJog(false);
    reset();
    run(e, 96);
    bool apart = true;
    for (auto& a : events)
        for (auto& b : events)
            apart &= !(a.on && b.on && a.channel == 1 && b.channel == 2 && a.tick == b.tick);
    CHECK(apart && onsOf(1) == 2 && onsOf(2) == 2);
    e.stop();
}

// Jog's pads: each takes the jogged channels on to their next pulse,
// played; its note still ends after its gate, in time.
static void testJogPulse()
{
    Engine e = makeSeqEngine(Key{0, 2}, {60, 64});
    Rhythm r;
    r.steps = 8;
    r.pulses = 2; // x . . . x . . .
    e.setRhythm(0, r);
    const uint8_t notes[] = { 60, 64 };
    e.setNotes(0, notes, 2);
    reset();
    e.start();
    run(e, 10); // the first pulse played
    CHECK(e.startJog());
    reset();
    run(e, 48 * 8); // a cycle of the clock: nothing from it
    CHECK(playedNotes().empty());
    e.jogPulse();
    CHECK((playedNotes() == std::vector<uint8_t>{ 64 }));
    e.jogPulse();
    CHECK((playedNotes() == std::vector<uint8_t>{ 64, 60 }));
    run(e, 48); // the clock ends the note (its gate), nothing more plays
    bool ended = false;
    for (auto& ev : events)
        ended |= !ev.on && ev.note == 60;
    CHECK(ended && playedNotes().size() == 2);
    e.stopJog(true);
    e.stop();
}

// The tools' channel toggles: a channel left out plays on as it was.
static void testOverrideMasks()
{
    Engine e = makeEngine();
    e.setGlobalKey(Key{0, 2});
    Rhythm r;
    r.steps = 4;
    r.pulses = 4;
    for (int ch = 0; ch < 2; ch++)
    {
        e.setRhythm(ch, r);
        Expression x = e.getExpression(ch);
        x.enabled = true;
        x.midiChannel = static_cast<uint8_t>(ch + 1);
        e.setExpression(ch, x);
    }
    const uint8_t n = 60;
    e.setNotes(0, &n, 1);
    e.setNotes(1, &n, 1);
    e.setChordOverride(static_cast<int8_t>(ChordType::triad));
    e.setOverrideMasks(0xFF, 0x02, 0xFF); // the chord on channel 2 only
    reset();
    e.start();
    run(e, 10);
    e.stop();
    int on1 = 0, on2 = 0;
    for (auto& ev : events)
        if (ev.on)
            (ev.channel == 1 ? on1 : on2)++;
    CHECK(on1 == 1 && on2 == 3);
}

static void testNotePoolAndLastNotes()
{
    // C major from C3 (note 36), one octave: the seven notes.
    Engine e = makeGeneratedEngine(NoteMode::scaleUp);
    e.setGlobalKey(Key{0, 2});
    uint8_t pool[32];
    CHECK(e.getNotePool(0, pool, 32) == 7 && pool[0] == 36 && pool[2] == 40 && pool[6] == 47);
    // Arps: the chord's tones (a triad for Single): C E G.
    Expression x = e.getExpression(0);
    Rhythm r = e.getRhythm(0);
    r.mode = NoteMode::arpUp;
    e.setRhythm(0, r);
    CHECK(e.getNotePool(0, pool, 32) == 3 && pool[1] == 40 && pool[2] == 43);
    // SEQ has its own list.
    r.mode = NoteMode::seq;
    e.setRhythm(0, r);
    CHECK(e.getNotePool(0, pool, 32) == 0);

    // The last notes stay after the gate closes.
    r.mode = NoteMode::scaleUp;
    e.setRhythm(0, r);
    reset();
    e.start();
    run(e, 48 + 40); // the second pulse is playing (D)
    uint8_t last[8];
    CHECK(e.getLastNotes(0, last) == 1 && last[0] == 38);
    const uint32_t n = e.getNoteCount(0);
    run(e, 30);
    CHECK(e.getLastNotes(0, last) == 1 && last[0] == 40 && e.getNoteCount(0) == n + 1);
    e.stop();
    (void)x;
}

static void testShiftedNoteShown()
{
    // Per cycle: 0 then +12 (4 pulses a cycle).
    Engine e = makeSeqEngine(Key{0, 2}, {60});
    Expression x = e.getExpression(0);
    x.shift.on = true;
    x.shift.length = 2;
    x.shift.advance = ShiftAdvance::perCycle;
    x.shift.step[1] = 12;
    e.setExpression(0, x);
    CHECK(e.getShiftAmount(0) == 0 && e.shiftedNote(0, 60) == 60); // stopped: as written
    reset();
    e.start();
    run(e, 48 * 2);
    CHECK(e.getShiftAmount(0) == 0 && e.shiftedNote(0, 62) == 62);
    run(e, 48 * 3);
    CHECK(e.getShiftAmount(0) == 12 && e.shiftedNote(0, 62) == 74);
    // In scale steps: C major, +2 from E is G.
    x.shift.units = ShiftUnits::scaleSteps;
    x.shift.step[1] = 2;
    e.setImmediate(true);
    e.setExpression(0, x);
    e.setImmediate(false);
    run(e, 1);
    CHECK(e.shiftedNote(0, 64) == 67);
    e.stop();
}

static void testStepsByHand()
{
    // E(4,16) has pulses on 0, 4, 8, 12. Mute 4, add 2.
    reset();
    Engine e = makeEngine();
    Rhythm r;
    r.steps = 16;
    r.pulses = 4;
    e.setRhythm(0, r);
    e.setEnabled(0, true);
    e.toggleStep(0, 4);
    e.toggleStep(0, 2);
    Rhythm s = e.getRhythm(0);
    CHECK(s.forceOff == (1u << 4) && s.forceOn == (1u << 2));
    CHECK(stepIsPulse(s, 2) && !stepIsPulse(s, 4) && stepIsPulse(s, 0));
    e.start();
    run(e, 48 * 16);
    e.stop();
    CHECK((onTicks(1) == std::vector<uint32_t>{0, 96, 384, 576}));
    // Again: back to the pattern.
    e.toggleStep(0, 4);
    CHECK(e.getRhythm(0).forceOff == 0 && e.getRhythm(0).forceOn == (1u << 2));
    // A new pattern, even the same pulses again, clears the hand changes.
    e.setPulses(0, 4);
    CHECK(e.getRhythm(0).forceOn == 0 && e.getRhythm(0).forceOff == 0);

    // While playing: staged, then live at the launch point after a commit.
    e.start();
    run(e, 48 * 3);
    e.toggleStep(0, 4); // mute step 4
    CHECK(e.getLiveRhythm(0).forceOff == 0);
    e.commitAll();
    run(e, 48 * 16 * 2);
    CHECK(e.getLiveRhythm(0).forceOff == (1u << 4));
    // Immediate (Shift): live straight away.
    e.setImmediate(true);
    e.toggleStep(0, 4);
    e.setImmediate(false);
    CHECK(e.getLiveRhythm(0).forceOff == 0);
    e.stop();
}

// Freeze and morph: holding, gliding a single note, a chord a voice at a
// time, and carrying on from whichever notes it's left on.
static std::vector<std::pair<int16_t, uint8_t>> bends;
static void onBend(int16_t v, uint8_t ch) { bends.push_back({v, ch}); }

static std::set<uint8_t> sounding()
{
    std::set<uint8_t> s;
    for (auto& ev : events)
        if (ev.on)
            s.insert(ev.note);
        else
            s.erase(ev.note);
    return s;
}

static void testFreezeGlide()
{
    Output o;
    o.noteOn = onNote;
    o.noteOff = offNote;
    o.pitchBend = onBend;
    o.randomPercent = zeroPercent;
    o.randomBelow = zeroBelow;
    Engine g;
    g.init(o);
    g.setGlobalKey(Key{0, 2});
    Rhythm r;
    r.steps = 4;
    r.pulses = 4;
    g.setRhythm(0, r);
    const uint8_t notes[] = { 60, 64, 67 };
    g.setNotes(0, notes, 3);
    g.setEnabled(0, true);
    g.setBendRange(2);
    reset();
    bends.clear();
    g.start();
    run(g, 10);
    static Engine scratch;
    CHECK(sounding() == std::set<uint8_t>({ 60 }));
    g.freeze(scratch);
    CHECK(g.isFrozen());
    uint8_t next[8];
    CHECK(g.getNextNotesFor(0, next) == 1 && next[0] == 64);
    const size_t before = events.size();
    run(g, 200); // frozen: nothing happens, the note holds
    CHECK(events.size() == before && sounding() == std::set<uint8_t>({ 60 }));
    g.setMorph(500); // 62: a whole-tone bend, the range
    CHECK(!bends.empty() && bends.back().first == 8191 && events.size() == before);
    g.setMorph(1000); // 64: past the range, struck afresh
    CHECK(sounding() == std::set<uint8_t>({ 64 }) && bends.back().first == 0);
    g.unfreeze();
    CHECK(!g.isFrozen());
    // Carries on after the next notes: 67 comes next, not 64 again.
    reset();
    run(g, 48);
    bool got67 = false, got64 = false;
    for (auto& ev : events)
        if (ev.on)
        {
            got67 |= ev.note == 67;
            got64 |= ev.note == 64;
        }
    CHECK(got67 && !got64);
    g.stop();
}

// Fine pitch: a pitch bend, the amount a share of the bend range; a free
// rate runs on the clock (here a millisecond a micro-step), not the bars.
static uint32_t testMillis() { return now; }
static void testFinePitchFreeRate()
{
    Mod m;
    m.dest = ModDest::finePitch;
    m.shape = ModShape::square;
    m.amount = 50;
    m.rate = ModRate::hz1; // 1000 ms a cycle
    Engine e = makeModEngine(m);
    Output o;
    o.noteOn = onNoteRecord;
    o.noteOff = offNote;
    o.pitchBend = onBend;
    o.millis = testMillis;
    e.init(o);
    Expression x;
    x.enabled = true;
    x.mod[0] = m;
    e.setExpression(0, x);
    Rhythm r;
    r.steps = 4;
    r.pulses = 4;
    e.setRhythm(0, r);
    reset();
    bends.clear();
    e.start();
    run(e, 100);
    CHECK(!bends.empty() && bends.back().first == 4095); // first half: up, half the range
    run(e, 500);                                         // 600 ms: second half
    CHECK(bends.back().first == -4095);
    run(e, 500); // 1100 ms: up again
    CHECK(bends.back().first == 4095);
    e.stop();
    CHECK(bends.back().first == 0);
    CHECK(isFreeRate(ModRate::hz12) && !isFreeRate(ModRate::bar64));
    CHECK(modRateNearest(kMicroStepsPerBar * 64) == ModRate::bar64);
}

// A delay and fade: after each note, nothing, then up to the full amount.
static void testFinePitchDelay()
{
    Mod m;
    m.dest = ModDest::finePitch;
    m.shape = ModShape::square;
    m.amount = 50;
    m.rate = ModRate::hz1;
    m.delay = 4; // 200 ms
    m.fade = 4;  // then 200 ms up
    CHECK(modTimeMs(4) == 200);
    Output o;
    o.noteOn = onNoteRecord;
    o.noteOff = offNote;
    o.pitchBend = onBend;
    o.millis = testMillis;
    Engine e;
    e.init(o);
    Rhythm r;
    r.steps = 4;
    r.pulses = 1;
    r.division = Division::d1_4; // a note a bar: 768 ms here
    e.setRhythm(0, r);
    Expression x;
    x.enabled = true;
    x.mod[0] = m;
    e.setExpression(0, x);
    reset();
    bends.clear();
    e.start();
    run(e, 150);
    CHECK(bends.empty()); // still in the delay
    run(e, 150);          // 300 ms: halfway up
    CHECK(!bends.empty() && bends.back().first > 1900 && bends.back().first < 2200);
    run(e, 150); // 450: all the way (the square is still up)
    CHECK(bends.back().first == 4095);
    run(e, 768 - 450 + 100); // the next note, 100 ms on: in the delay again
    CHECK(bends.back().first == 0);
    e.stop();
}

// Freeze on some channels only: those hold, keeping their place in the
// rhythm; the others, and the clock, play on.
static void testFreezeSomeChannels()
{
    Engine e = makeEngine();
    e.setGlobalKey(Key{0, 2});
    Rhythm r;
    r.steps = 4;
    r.pulses = 4;
    const uint8_t a[] = { 60, 64, 67 };
    const uint8_t b[] = { 48, 50 };
    for (int ch = 0; ch < 2; ch++)
    {
        e.setRhythm(ch, r);
        Expression x = e.getExpression(ch);
        x.enabled = true;
        x.midiChannel = static_cast<uint8_t>(ch + 1);
        e.setExpression(ch, x);
    }
    e.setNotes(0, a, 3);
    e.setNotes(1, b, 2);
    e.setFreezeMask(0x01);
    reset();
    e.start();
    run(e, 10);
    static Engine scratch;
    e.freeze(scratch);
    CHECK(e.isFrozen() && e.isChannelFrozen(0) && !e.isChannelFrozen(1) && !e.isClockHeld());
    uint8_t steps = 0;
    e.getMorphStep(1, &steps);
    CHECK(steps == 0); // nothing to morph on a channel playing on
    reset();
    run(e, 48 * 4); // a bar: channel 2 plays its four, channel 1 nothing
    int on1 = 0, on2 = 0;
    for (auto& ev : events)
        if (ev.on)
            (ev.channel == 1 ? on1 : on2)++;
    CHECK(on1 == 0 && on2 == 4);
    e.unfreeze();
    // Back in time: the next note on the beat, the one after the held 60.
    reset();
    run(e, 48);
    // (10 + 192 micro-steps in: the beat is 38 on, with channel 2's note.)
    bool got64 = false, beat2 = false;
    for (auto& ev : events)
    {
        got64 |= ev.on && ev.channel == 1 && ev.note == 64 && ev.tick == 38;
        beat2 |= ev.on && ev.channel == 2 && ev.tick == 38;
    }
    CHECK(got64 && beat2);
    // Every channel playing held: the clock is.
    e.setFreezeMask(0xFF);
    e.freeze(scratch);
    CHECK(e.isClockHeld());
    e.unfreeze();
    e.stop();
}

// Further on: Notes ahead 3 goes through the next three, 1000 a note.
static void testFreezeReach()
{
    Output o;
    o.noteOn = onNote;
    o.noteOff = offNote;
    o.pitchBend = onBend;
    o.randomPercent = zeroPercent;
    o.randomBelow = zeroBelow;
    Engine g;
    g.init(o);
    g.setGlobalKey(Key{0, 2});
    Rhythm r;
    r.steps = 4;
    r.pulses = 4;
    g.setRhythm(0, r);
    const uint8_t notes[] = { 60, 64, 67, 72 };
    g.setNotes(0, notes, 4);
    g.setEnabled(0, true);
    g.setBendRange(2);
    CHECK(g.getMorphReach() == 3);
    reset();
    g.start();
    run(g, 10);
    static Engine scratch;
    g.freeze(scratch);
    uint8_t steps = 0;
    g.getMorphStep(0, &steps);
    CHECK(steps == 3);
    g.setMorph(2000);
    CHECK(sounding() == std::set<uint8_t>({ 67 }));
    g.setMorph(9000); // no further than the third
    CHECK(g.getMorph() == 3000 && sounding() == std::set<uint8_t>({ 72 }));
    g.setMorph(1900); // nearest the second: 67, and on from there
    g.unfreeze();
    reset();
    run(g, 48);
    bool got72 = false;
    for (auto& ev : events)
        got72 |= ev.on && ev.note == 72;
    CHECK(got72);
    g.stop();
    g.setMorphReach(9);
    CHECK(g.getMorphReach() == 4);
}

// Freeze's Jump: the notes change as the wheel passes halfway, no bends.
static void testFreezeJump()
{
    Output o;
    o.noteOn = onNote;
    o.noteOff = offNote;
    o.pitchBend = onBend;
    o.randomPercent = zeroPercent;
    o.randomBelow = zeroBelow;
    Engine g;
    g.init(o);
    g.setGlobalKey(Key{0, 2});
    Rhythm r;
    r.steps = 4;
    r.pulses = 4;
    g.setRhythm(0, r);
    const uint8_t notes[] = { 60, 64, 67 };
    g.setNotes(0, notes, 3);
    g.setEnabled(0, true);
    g.setMorphJump(true);
    reset();
    bends.clear();
    g.start();
    run(g, 10);
    static Engine scratch;
    g.freeze(scratch);
    g.setMorph(400);
    CHECK(sounding() == std::set<uint8_t>({ 60 }));
    g.setMorph(600);
    CHECK(sounding() == std::set<uint8_t>({ 64 }) && bends.empty());
    g.setMorph(1600);
    CHECK(sounding() == std::set<uint8_t>({ 67 }) && bends.empty());
    g.unfreeze();
    g.stop();
}

static void testFreezeChord()
{
    // Two chord entries: 60 64 67, then 62 65 69 (ties join a chord).
    Engine e = makeEngine();
    e.setGlobalKey(Key{0, 2});
    Rhythm r;
    r.steps = 4;
    r.pulses = 4;
    e.setRhythm(0, r);
    const uint8_t notes[] = { 60, 64, 67, 62, 65, 69 };
    e.setNotes(0, notes, 6, 0x36);
    e.setEnabled(0, true);
    reset();
    e.start();
    run(e, 10);
    CHECK(sounding() == std::set<uint8_t>({ 60, 64, 67 }));
    static Engine scratch;
    e.freeze(scratch);
    uint8_t next[8];
    CHECK(e.getNextNotesFor(0, next) == 3);
    e.setMorph(500); // two of three voices: 64->65 (a semitone) first, then 60->62
    CHECK(sounding() == std::set<uint8_t>({ 62, 65, 67 }));
    e.setMorph(1000);
    CHECK(sounding() == std::set<uint8_t>({ 62, 65, 69 }));
    e.setMorph(300); // back towards the held chord: one voice moved
    CHECK(sounding() == std::set<uint8_t>({ 60, 65, 67 }));
    e.unfreeze(); // nearer the held chord: back to it, and on as before
    CHECK(sounding() == std::set<uint8_t>({ 60, 64, 67 }));
    reset();
    run(e, 48);
    bool got62 = false;
    for (auto& ev : events)
        got62 |= ev.on && ev.note == 62;
    CHECK(got62);
    e.stop();
}

// Long gates and legato: a note can last up to 8 steps, or hold until the
// next pulse (overlapping it).
static void testLongGatesAndLegato()
{
    // One pulse in 8 steps (48 micro-steps each), gate 300%: three steps.
    Engine e = makeEngine();
    Rhythm r;
    r.steps = 8;
    r.pulses = 1;
    e.setRhythm(0, r);
    e.setEnabled(0, true);
    e.setGate(0, 120); // 100 + 2*10 = 120 -> 300%
    CHECK(gatePercentOf(120) == 300 && gatePercentOf(170) == 800 && gatePercentOf(50) == 50);
    reset();
    e.start();
    run(e, 48 * 8);
    e.stop();
    uint32_t on = 0, off = 0;
    for (auto& ev : events)
        if (ev.channel == 1)
            (ev.on ? on : off) = ev.tick;
    CHECK(on == 0 && off == 144);

    // Legato, 2 pulses in 4 steps (x.x.): each note holds through the rest
    // and the next starts before it ends.
    Engine g = makeSeqEngine(Key{0, 2}, {60, 64});
    Rhythm r2 = g.getRhythm(0);
    r2.pulses = 2;
    g.setRhythm(0, r2);
    g.setGate(0, kGateLegato);
    reset();
    g.start();
    run(g, 48 * 3);
    // 60 on at 0, still on through tick 48 (a rest); at 96: 64 on, then 60 off.
    std::vector<Event> seen = events;
    CHECK(seen.size() == 3);
    if (seen.size() == 3)
    {
        CHECK(seen[0].on && seen[0].note == 60 && seen[0].tick == 0);
        CHECK(seen[1].on && seen[1].note == 64 && seen[1].tick == 96);
        CHECK(!seen[2].on && seen[2].note == 60 && seen[2].tick == 96);
    }
    g.stop();
    CHECK(sounding().empty());

    // The same note again, legato: released, then struck (no double note-on).
    Engine h = makeSeqEngine(Key{0, 2}, {60});
    h.setGate(0, kGateLegato);
    reset();
    h.start();
    run(h, 48 * 2);
    h.stop();
    CHECK(events.size() >= 3 && events[0].on && !events[1].on && events[2].on && events[1].tick == 48 &&
          events[2].tick == 48);
}

static void testShiftInScaleSteps()
{
    Engine e = makeSeqEngine(Key{0, 2}, {60}); // C major
    e.setGlobalKey(Key{0, 2});
    Expression x = e.getExpression(0);
    x.shift.on = true;
    x.shift.length = 2;
    x.shift.units = ShiftUnits::scaleSteps;
    x.shift.step[0] = 2;  // C -> E
    x.shift.step[1] = -1; // C -> B
    e.setExpression(0, x);
    CHECK((playSeq(e, 2) == std::vector<uint8_t>{64, 59}));
}

static void testShiftCountsTowardsRepeatLength()
{
    Engine e = makeSeqEngine(Key{0, 1}, {60});
    Expression x = e.getExpression(0);
    x.shift.on = true;
    x.shift.length = 3; // 3 steps against a 4-step rhythm: 12 steps
    e.setExpression(0, x);
    e.start();
    CHECK(e.repeatLength(0) == 12u * kMicroStepsPerStep);
    e.stop();
}

int main()
{
    testShiftPerStepDriftsAgainstTheRhythm();
    testShiftPerPulseSkipsRests();
    testShiftPerCycleHoldsForTheCycle();
    testChordLanePerPulse();
    testDivisionLanePerCycle();
    testModeLaneAndNone();
    testSamplerOutput();
    testSampleLane();
    testPulsesLane();
    testModeOverride();
    testFreezeJump();
    testChordOverride();
    testPulseBoost();
    testJog();
    testJogPulse();
    testOverrideMasks();
    testNotePoolAndLastNotes();
    testShiftedNoteShown();
    testStepsByHand();
    testFreezeGlide();
    testFreezeChord();
    testFreezeReach();
    testFreezeSomeChannels();
    testFinePitchFreeRate();
    testFinePitchDelay();
    testLongGatesAndLegato();
    testShiftInScaleSteps();
    testShiftCountsTowardsRepeatLength();
    testMuteAtLaunch();
    testSineIsBipolarAndBarLocked();
    testPhaseOffset();
    testModAmountScalesAnother();
    testModRateSpeedsAnotherUp();
    testTwoModulatorsOnOneCcAddUp();
    testAccentOnVelocity();
    testAccentOnPitchIsAnOctave();
    testAutoRotate();
    testEnvelopeDecays();
    testCcSquareLfo();
    testScaleUpWithTriads();
    testArps();
    testChordChance();
    testSeqChordEntries();
    testSeqOwnIgnoresGlobalKey();
    testSeqFollowMovesWithTheRoot();
    testSeqFollowSnapsToTheScale();
    testSeqOffsetAndTranspose();
    testTransposeGeneratedMode();
    testStagedChangesWaitForCommit();
    testImmediateExpressionPlaysNow();
    testCommitOneChannel();
    testLaunchBeat();
    testMuteIsNeverStaged();
    testStagedGlobalKey();
    testScaleUpInCMajor();
    testScaleUpDownAndOffsetKey();
    testRandomAndWalkStayInKeyAndRange();
    testGlobalKeyWaitsForBar();
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
