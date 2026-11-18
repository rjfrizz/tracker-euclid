// Host-side tests for freezing. Not part of the firmware build.
//   c++ -std=c++17 -I core/source core/tests/euclid/euclidFreezeTest.cpp \
//       core/source/euclid/euclidEngine.cpp core/source/euclid/euclidFreeze.cpp -o /tmp/freezeTest

#include "euclid/euclidFreeze.h"

#include <cstdio>
#include <vector>

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

static uint8_t zeroPercent() { return 0; }
static uint8_t lcgBelow(uint8_t n)
{
    static uint32_t s = 777;
    s = s * 1103515245u + 12345u;
    return static_cast<uint8_t>((s >> 16) % n);
}

static std::vector<NoteEvent> buffer(20000);
static FrozenPattern frozen; // big: keep it off the stack

static Engine makeEngine(uint32_t capacity = 20000)
{
    Output o;
    o.randomPercent = zeroPercent;
    o.randomBelow = lcgBelow;
    Engine e;
    e.init(o);
    e.setCaptureBuffer(buffer.data(), capacity);
    for (int ch = 0; ch < kChannels; ch++)
        e.setEnabled(ch, false);
    return e;
}

static void setChannel(Engine& e, int ch, uint8_t steps, uint8_t pulses, std::vector<uint8_t> notes,
                       Division div = Division::d1_16, uint8_t gate = 50)
{
    Rhythm r;
    r.steps = steps;
    r.pulses = pulses;
    r.division = div;
    e.setRhythm(ch, r);
    e.setNotes(ch, notes.data(), static_cast<uint8_t>(notes.size()));
    Expression x = e.getExpression(ch);
    x.enabled = true;
    x.midiChannel = static_cast<uint8_t>(ch + 1);
    x.velocity = 127;
    x.gate = gate;
    e.setExpression(ch, x);
}

static void play(Engine& e, uint32_t microSteps)
{
    e.start();
    for (uint32_t i = 0; i < microSteps; i++)
        e.onMicroStep();
    e.stop(); // so edits between takes apply at once rather than being staged
}

static int notesOn(const FrozenTrack& t)
{
    int n = 0;
    for (int s = 0; s < t.length; s++)
        n += t.step[s].note >= 0;
    return n;
}

static void testPolyrhythmLoops()
{
    Engine e = makeEngine();
    setChannel(e, 0, 16, 4, {36});
    setChannel(e, 1, 13, 5, {50});
    play(e, kMicroStepsPerBar * 5);
    buildFreeze(e, 0, kMicroStepsPerBar * 4, frozen);

    CHECK(frozen.length == 64);
    CHECK(frozen.channels == 2);
    const FrozenTrack& kick = frozen.track[kFreezeFirstMidiTrack + 0];
    const FrozenTrack& poly = frozen.track[kFreezeFirstMidiTrack + 1];
    CHECK(kick.used && kick.length == 16 && notesOn(kick) == 4);
    CHECK(poly.used && poly.length == 13 && notesOn(poly) == 5);
    for (int s = 0; s < 13; s++)
        CHECK((poly.step[s].note >= 0) == isPulse(13, 5, 0, static_cast<uint8_t>(s)));
    CHECK(poly.step[0].note == 50 && poly.step[0].midiChannel == 2);
    CHECK(poly.step[0].volume == 100 && poly.step[0].micromove == -1);
    CHECK(poly.step[0].gate == 51); // 24 of 48 micro-steps
    CHECK(frozen.tracksUsed == 2);
}

static void testRandomIsATake()
{
    Engine e = makeEngine();
    setChannel(e, 0, 4, 4, {60});
    Rhythm r = e.getRhythm(0);
    r.mode = NoteMode::walk;
    e.setRhythm(0, r);
    play(e, kMicroStepsPerBar * 2);
    buildFreeze(e, 0, kMicroStepsPerBar * 2, frozen);
    const FrozenTrack& t = frozen.track[kFreezeFirstMidiTrack];
    CHECK(t.length == 32); // the whole window
    CHECK(frozen.loopingTracks == 0);
}

static void testChordsUseStockShapesOrSpill()
{
    {
        const uint8_t triad[] = {43, 36, 40};
        uint8_t root = 0;
        CHECK(matchStockChord(triad, 3, &root) == 5); // "47": major
        CHECK(root == 36);
    }
    {
        const uint8_t wide[] = {36, 40, 43, 47, 50}; // a 9th: no stock shape
        CHECK(matchStockChord(wide, 5, nullptr) == 0);
    }

    Engine e = makeEngine();
    e.setGlobalKey(Key{0, 2}); // before the notes, so their home key is C major
    setChannel(e, 0, 4, 4, {36});
    Expression x = e.getExpression(0);
    x.chord = ChordType::triad;
    e.setExpression(0, x);
    play(e, kMicroStepsPerBar);
    buildFreeze(e, 0, kMicroStepsPerBar, frozen);
    CHECK(frozen.tracksUsed == 1);
    CHECK(frozen.track[8].step[0].note == 36 && frozen.track[8].step[0].chord == 5);

    // The same with 9ths: 5 voices, the extras on channels' free MIDI tracks.
    x.chord = ChordType::ninth;
    e.setExpression(0, x);
    play(e, kMicroStepsPerBar);
    buildFreeze(e, 0, kMicroStepsPerBar, frozen);
    CHECK(frozen.tracksUsed == 5);
    CHECK(frozen.track[8].step[0].note == 36 && frozen.track[8].step[0].chord == 0);
    CHECK(frozen.track[9].used && frozen.track[9].voice == 1 && frozen.track[9].step[0].note == 40);
    CHECK(frozen.track[12].used && frozen.track[12].step[0].note == 50);
    CHECK(frozen.droppedNotes == 0);
}

static void testThirtySecondsAndLongGates()
{
    Engine e = makeEngine();
    setChannel(e, 0, 2, 2, {60}, Division::d1_32);
    play(e, kMicroStepsPerBar);
    buildFreeze(e, 0, kMicroStepsPerBar, frozen);
    // Two notes per step: the second, half a step late, on a second track.
    CHECK(frozen.tracksUsed == 2);
    const FrozenTrack& late = frozen.track[9];
    CHECK(late.used && late.step[0].note == 60);
    // The player delays by map(value + 1, 0, 100, 1, 48): half a step.
    CHECK((late.step[0].micromove + 1) * 47 / 100 + 1 == 24);
    CHECK(frozen.track[8].step[0].micromove == -1);

    Engine g = makeEngine();
    setChannel(g, 0, 4, 1, {48}, Division::d1_8, 100); // 96-micro-step notes every 4 eighths
    play(g, kMicroStepsPerBar * 2);
    buildFreeze(g, 0, kMicroStepsPerBar * 2, frozen);
    const FrozenTrack& t = frozen.track[8];
    CHECK(t.step[0].note == 48 && t.step[0].gate == -1);
    CHECK(t.step[2].note == FrozenStep::kNoteOff); // two steps later
}

static void testTooManyVoicesDropsHighest()
{
    Engine e = makeEngine();
    e.setGlobalKey(Key{0, 2});
    for (int ch = 0; ch < kChannels; ch++)
    {
        setChannel(e, ch, 4, 4, {static_cast<uint8_t>(36 + ch)});
        Expression x = e.getExpression(ch);
        x.chord = ChordType::ninth; // C 9th and friends: 5 notes, no stock shape
        e.setExpression(ch, x);
    }
    play(e, kMicroStepsPerBar);
    buildFreeze(e, 0, kMicroStepsPerBar, frozen);
    // Every track but track 0: the channels loop at 4 steps, and track 0
    // sets the pattern's length, so it only takes full-length voices.
    CHECK(frozen.tracksUsed == 15);
    CHECK(frozen.track[0].used == false);
    CHECK(frozen.droppedNotes == (40 - 15) * 4); // 25 notes from each of the 4 chords
    CHECK(frozen.channels == 8);
}

static void testIncompleteWhenTheBufferWrapped()
{
    Engine e = makeEngine(32);
    setChannel(e, 0, 4, 4, {60});
    play(e, kMicroStepsPerBar * 4); // 64 notes into a 32-note buffer
    buildFreeze(e, 0, kMicroStepsPerBar * 4, frozen);
    CHECK(frozen.incomplete);
}

int main()
{
    testPolyrhythmLoops();
    testRandomIsATake();
    testChordsUseStockShapesOrSpill();
    testThirtySecondsAndLongGates();
    testTooManyVoicesDropsHighest();
    testIncompleteWhenTheBufferWrapped();
    if (failures == 0)
        std::printf("all euclid freeze tests passed\n");
    return failures == 0 ? 0 : 1;
}
