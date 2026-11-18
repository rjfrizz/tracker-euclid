// Freezing on the Tracker: the capture buffer, and writing frozen tracks
// into a new stock pattern in the workspace.

#include "euclidPlatform.h"

#include "euclidEngine.h"
#include "euclidFreeze.h"
#include "fileManager.h"
#include "mtSequencer.h"
#include "mtStructs.h"

#include <cr_section_macros.h>
#include <new>
#include <stdio.h>

namespace
{
// 8 bars of 8 channels playing 6-note chords on every 1/32 would be 24,576
// notes; 12,288 covers 8 bars of anything short of that, and a freeze says
// so when the oldest notes of its window had already been overwritten.
constexpr uint32_t kCaptureCapacity = 12288;

__NOINIT(EXTERNAL_RAM) euclid::NoteEvent captureBuffer[kCaptureCapacity];
// FrozenPattern has default values, which a no-init section can't hold, so
// it gets raw space here and is constructed when a freeze runs.
__NOINIT(EXTERNAL_RAM) alignas(euclid::FrozenPattern) uint8_t frozenSpace[sizeof(euclid::FrozenPattern)];
__NOINIT(EXTERNAL_RAM) Sequencer::strPattern pattern;

using Fx = Sequencer::strFxConsts;

void fillPattern(const euclid::FrozenPattern& f, Sequencer::strPattern& p)
{
    sequencer.clearPattern(&p);

    const uint8_t old = p.track[0].length;
    p.track[0].length = static_cast<uint8_t>(f.length - 1);
    sequencer.applyPatternLength(&p, old);

    for (int t = 0; t < euclid::kFreezeTracks; t++)
    {
        const euclid::FrozenTrack& ft = f.track[t];
        if (!ft.used)
            continue;
        if (ft.length < f.length)
            sequencer.setTrackLength(&p, static_cast<uint8_t>(t), static_cast<uint8_t>(ft.length - 1));

        for (int s = 0; s < ft.length; s++)
        {
            const euclid::FrozenStep& fs = ft.step[s];
            auto& step = p.track[t].step[s];

            if (fs.note == euclid::FrozenStep::kNoteOff)
            {
                step.note = Sequencer::STEP_NOTE_OFF_DEFAULT;
                continue;
            }
            if (fs.note < 0)
                continue;

            step.note = fs.note;
            step.instrument = static_cast<uint8_t>(MIDI_INSTRUMENTS_START_IDX + fs.midiChannel - 1);
            step.volume = static_cast<int8_t>(fs.volume);
            step.micromove = fs.micromove;

            uint8_t slot = 0;
            if (fs.gate >= 0)
            {
                step.fx[slot].type = Fx::FX_TYPE_GATE_LENGTH;
                step.fx[slot].value = static_cast<uint8_t>(fs.gate);
                slot++;
            }
            if (fs.chord != 0)
            {
                step.fx[slot].type = Fx::FX_TYPE_MIDI_CHORD;
                step.fx[slot].value = fs.chord;
            }
        }
    }
}
} // namespace

void euclidFreezeInit()
{
    euclidEngine.setCaptureBuffer(captureBuffer, kCaptureCapacity);
}

namespace euclidFreeze
{
bool freeze(uint32_t start, uint32_t end, char* message, size_t messageLength)
{
    const uint8_t bars = static_cast<uint8_t>((end - start) / euclid::kMicroStepsPerBar);

    euclid::FrozenPattern& frozen = *new (frozenSpace) euclid::FrozenPattern();
    euclid::buildFreeze(euclidEngine, start, end, frozen);
    if (frozen.channels == 0)
    {
        snprintf(message, messageLength, "Nothing played: nothing frozen");
        return false;
    }

    const uint8_t slot = newFileManager.findFreePatternSlot();
    if (slot == 0)
    {
        snprintf(message, messageLength, "No free pattern slot");
        return false;
    }

    fillPattern(frozen, pattern);
    if (!newFileManager.writeFrozenPattern(slot, &pattern))
    {
        snprintf(message, messageLength, "Freeze failed: check SD card");
        return false;
    }

    int n = snprintf(message, messageLength, "Pattern %d: %d bar%s, %d tracks", slot, bars,
                     bars == 1 ? "" : "s", frozen.tracksUsed);
    if (frozen.droppedNotes > 0 && n > 0 && static_cast<size_t>(n) < messageLength)
        n += snprintf(message + n, messageLength - n, ", %d dropped", frozen.droppedNotes);
    if (frozen.incomplete && n > 0 && static_cast<size_t>(n) < messageLength)
        snprintf(message + n, messageLength - n, ", start lost");
    return true;
}
} // namespace euclidFreeze
