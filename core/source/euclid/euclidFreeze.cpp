#include "euclidFreeze.h"

#include <stdint.h>

#include "mtChordDefs.h"

namespace euclid
{

namespace
{
constexpr uint32_t kStep = kMicroStepsPerStep; // one pattern step = 1/16

uint32_t gcd32(uint32_t a, uint32_t b)
{
    while (b != 0)
    {
        uint32_t t = a % b;
        a = b;
        b = t;
    }
    return a;
}

bool sameNote(const NoteEvent& a, const NoteEvent& b)
{
    return a.note == b.note && a.velocity == b.velocity && a.gate == b.gate &&
           a.midiChannel == b.midiChannel;
}

// True if channel ch played exactly the same notes every `period`
// micro-steps through the window.
bool repeats(const Engine& e, int ch, uint32_t start, uint32_t end, uint32_t period)
{
    const uint32_t first = e.captureFirst();
    const uint32_t last = e.captureEnd();
    uint32_t earlier = 0, later = 0;

    for (uint32_t i = first; i < last; i++)
    {
        const NoteEvent& a = e.captureAt(i);
        if (a.channel != ch || a.time < start || a.time >= end)
            continue;
        if (a.time >= start + period)
            later++;
        if (a.time + period >= end)
            continue;
        earlier++;

        bool found = false;
        for (uint32_t j = i + 1; j < last && !found; j++)
        {
            const NoteEvent& b = e.captureAt(j);
            if (b.time > a.time + period)
                break;
            found = b.channel == ch && b.time == a.time + period && sameNote(a, b);
        }
        if (!found)
            return false;
    }
    return earlier == later;
}

// The micromove column value that delays a note by `offset` micro-steps,
// for the player's map(value + 1, 0, 100, 1, 48).
int8_t micromoveFor(uint32_t offset)
{
    if (offset == 0)
        return -1;
    int best = 0;
    int bestError = 1000;
    for (int m = 0; m <= 100; m++)
    {
        int delay = (m + 1) * 47 / 100 + 1;
        int error = delay > static_cast<int>(offset) ? delay - static_cast<int>(offset)
                                                     : static_cast<int>(offset) - delay;
        if (error < bestError)
        {
            bestError = error;
            best = m;
        }
    }
    return static_cast<int8_t>(best);
}

// A chord or a single note, placed on one track.
struct Item
{
    uint8_t note;
    uint8_t chord;     // stock Chord FX index, 0 for a single note
    uint8_t noteCount; // notes it stands for, for counting drops
};

class Allocator
{
  public:
    Allocator(FrozenPattern& p, const bool* channelPlayed) : p(p)
    {
        // Spare tracks for extra voices: tracks 9-16, then the tracks of
        // channels that played nothing (track 1 last: it sets the
        // pattern's length, so it only takes full-length voices).
        for (int t = kFreezeFirstVoiceTrack; t < kFreezeTracks; t++)
            pool[poolCount++] = static_cast<uint8_t>(t);
        for (int c = 1; c < kChannels; c++)
            if (!channelPlayed[c])
                pool[poolCount++] = static_cast<uint8_t>(c);
        if (!channelPlayed[0])
            pool[poolCount++] = 0;
    }

    // A spare track for a voice that loops at `length` steps, or -1.
    int take(uint8_t length)
    {
        for (int i = 0; i < poolCount; i++)
        {
            uint8_t t = pool[i];
            if (t == 0 && length != p.length)
                continue;
            for (int j = i; j + 1 < poolCount; j++)
                pool[j] = pool[j + 1];
            poolCount--;
            return t;
        }
        return -1;
    }

  private:
    FrozenPattern& p;
    uint8_t pool[kFreezeTracks];
    int poolCount = 0;
};
} // namespace

uint8_t matchStockChord(const uint8_t* notes, uint8_t count, uint8_t* root)
{
    if (count == 0)
        return 0;
    uint8_t low = notes[0];
    for (uint8_t i = 1; i < count; i++)
        if (notes[i] < low)
            low = notes[i];
    if (root != nullptr)
        *root = low;

    uint16_t mask = 0;
    for (uint8_t i = 0; i < count; i++)
    {
        uint8_t interval = static_cast<uint8_t>(notes[i] - low);
        if (interval >= CHORD_FOR_MAX)
            return 0;
        mask |= static_cast<uint16_t>(1u << interval);
    }
    for (uint8_t k = 1; k <= MAX_CHORD; k++)
        if (chordDef[k] == mask)
            return k;
    return 0;
}

void buildFreeze(const Engine& e, uint32_t start, uint32_t end, FrozenPattern& out)
{
    out.length = 0;
    out.tracksUsed = 0;
    out.channels = 0;
    out.loopingTracks = 0;
    out.droppedNotes = 0;
    out.incomplete = false;
    for (FrozenTrack& t : out.track)
    {
        t.used = false;
        t.channel = 0;
        t.voice = 0;
        t.length = 0;
        for (FrozenStep& s : t.step)
            s = FrozenStep();
    }

    if (end <= start)
        return;
    uint32_t steps = (end - start) / kStep;
    if (steps > kFreezeMaxSteps)
    {
        steps = kFreezeMaxSteps;
        start = end - steps * kStep;
    }
    out.length = static_cast<uint8_t>(steps);

    const uint32_t first = e.captureFirst();
    const uint32_t last = e.captureEnd();
    if (first > 0 && first < last && e.captureAt(first).time > start)
        out.incomplete = true;

    bool played[kChannels] = {};
    for (uint32_t i = first; i < last; i++)
    {
        const NoteEvent& n = e.captureAt(i);
        if (n.time >= start && n.time < end && n.channel < kChannels)
            played[n.channel] = true;
    }

    Allocator spare(out, played);

    for (int c = 0; c < kChannels; c++)
    {
        if (!played[c])
            continue;
        out.channels++;

        // Loop length: the channel's repeat length, if what it played really
        // did repeat through the window; otherwise the whole window.
        uint8_t length = out.length;
        uint32_t period = c == 0 ? 0 : e.repeatLength(c); // track 1 is the pattern's length
        if (period > 0)
        {
            period = period / gcd32(period, kStep) * kStep; // whole steps
            if (period < steps * kStep && repeats(e, c, start, end, period))
                length = static_cast<uint8_t>(period / kStep);
        }

        int lanes[kFreezeTracks];
        int laneCount = 0;
        lanes[laneCount++] = c;
        FrozenTrack& main = out.track[c];
        main.used = true;
        main.channel = static_cast<uint8_t>(c);
        main.voice = 0;
        main.length = length;

        const uint32_t loopEnd = start + length * kStep;

        for (uint32_t i = first; i < last;)
        {
            const NoteEvent& head = e.captureAt(i);
            if (head.channel != c || head.time < start || head.time >= loopEnd)
            {
                i++;
                continue;
            }

            // The channel's notes that started together.
            uint8_t notes[kMaxVoices];
            uint8_t count = 0;
            uint32_t j = i;
            for (; j < last; j++)
            {
                const NoteEvent& n = e.captureAt(j);
                if (n.time != head.time)
                    break;
                if (n.channel == c && count < kMaxVoices)
                    notes[count++] = n.note;
            }
            i = j;

            // Lowest first, so voices that don't fit lose their top notes.
            for (uint8_t a = 1; a < count; a++)
                for (uint8_t b = a; b > 0 && notes[b] < notes[b - 1]; b--)
                {
                    uint8_t t = notes[b];
                    notes[b] = notes[b - 1];
                    notes[b - 1] = t;
                }

            Item items[kMaxVoices];
            uint8_t itemCount = 0;
            uint8_t root = 0;
            uint8_t chord = count > 1 ? matchStockChord(notes, count, &root) : 0;
            if (chord != 0)
                items[itemCount++] = { root, chord, count };
            else
                for (uint8_t k = 0; k < count; k++)
                    items[itemCount++] = { notes[k], 0, 1 };

            const uint32_t rel = head.time - start;
            const uint32_t step = rel / kStep;
            const uint32_t offset = rel % kStep;

            for (uint8_t k = 0; k < itemCount; k++)
            {
                // The first voice track with this step free, or a new one.
                int track = -1;
                for (int l = 0; l < laneCount && track < 0; l++)
                {
                    int8_t cell = out.track[lanes[l]].step[step].note;
                    // A note-off can be replaced: a new note ends the old one too.
                    if (cell == FrozenStep::kEmpty || cell == FrozenStep::kNoteOff)
                        track = lanes[l];
                }
                if (track < 0 && laneCount < kFreezeTracks)
                {
                    track = spare.take(length);
                    if (track >= 0)
                    {
                        lanes[laneCount] = track;
                        FrozenTrack& t = out.track[track];
                        t.used = true;
                        t.channel = static_cast<uint8_t>(c);
                        t.voice = static_cast<uint8_t>(laneCount);
                        t.length = length;
                        laneCount++;
                    }
                }
                if (track < 0)
                {
                    out.droppedNotes += items[k].noteCount;
                    continue;
                }

                FrozenTrack& t = out.track[track];

                // This note ends whatever was sounding on the track, so an
                // earlier note's pending note-off further on must go.
                for (uint32_t q = step + 1; q < length; q++)
                {
                    if (t.step[q].note == FrozenStep::kEmpty)
                        continue;
                    if (t.step[q].note == FrozenStep::kNoteOff)
                        t.step[q] = FrozenStep();
                    break;
                }

                FrozenStep& s = t.step[step];
                s = FrozenStep();
                s.note = static_cast<int8_t>(items[k].note);
                s.midiChannel = head.midiChannel;
                s.volume = static_cast<uint8_t>((head.velocity * 100 + 63) / 127);
                s.micromove = micromoveFor(offset);
                s.chord = items[k].chord;

                // Gate: an FX within the step, or a note-off where a longer
                // note ends (if that step is still free on this track).
                if (head.gate < kStep)
                {
                    s.gate = static_cast<int16_t>((head.gate * 100 + 23) / 47);
                    if (s.gate > 100)
                        s.gate = 100;
                }
                else
                {
                    uint32_t offStep = (rel + head.gate) / kStep;
                    bool clear = offStep < length;
                    for (uint32_t q = step + 1; clear && q <= offStep; q++)
                        clear = t.step[q].note == FrozenStep::kEmpty;
                    if (clear && offStep > step)
                        t.step[offStep].note = FrozenStep::kNoteOff;
                }
            }
        }
    }

    for (const FrozenTrack& t : out.track)
    {
        if (!t.used)
            continue;
        out.tracksUsed++;
        if (t.length < out.length)
            out.loopingTracks++;
    }
}

} // namespace euclid
