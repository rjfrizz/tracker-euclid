// Connects the platform-independent Euclidean engine to the Tracker's MIDI
// output, random source and interrupt guard.

#include "euclidPlatform.h"

#include "config.h"
#include "euclidEngine.h"
#include "mtMidi.h"
#include "util_math.h"

namespace
{
void noteOn(uint8_t note, uint8_t velocity, uint8_t channel)
{
    sendMidiNoteOn(note, velocity, channel, false);
}

void noteOff(uint8_t note, uint8_t channel)
{
    sendMidiNoteOff(note, 0, channel, false);
}

uint8_t randomPercent()
{
    return static_cast<uint8_t>(utilMathRandom::random(0, 100));
}

uint8_t randomBelow(uint8_t n)
{
    return static_cast<uint8_t>(utilMathRandom::random(0, n));
}

void lock()
{
    DISABLE_GLOBAL_INTERRUPTS();
}

void unlock()
{
    ENABLE_GLOBAL_INTERRUPTS();
}
} // namespace

void euclidPlatformInit()
{
    euclid::Output out;
    out.noteOn = noteOn;
    out.noteOff = noteOff;
    out.randomPercent = randomPercent;
    out.randomBelow = randomBelow;
    out.lock = lock;
    out.unlock = unlock;
    euclidEngine.init(out);
    euclid::loadDemoDefaults(euclidEngine);
}
