#include "UartMidiImpl.h"
#include "MIDI.h"

// EUCLID: room for synth patch dumps (the stock 128 bytes is less than some
// synths' dump messages). Longer ones arrive in pieces.
struct EuclidMidiSettings : public MIDI_NAMESPACE::DefaultSettings
{
    static const unsigned SysExMaxSize = 512;
};

static MIDI_NAMESPACE::SerialMIDI<HardwareSerial5> serialMIDI(Serial5);
static MIDI_NAMESPACE::MidiInterface<MIDI_NAMESPACE::SerialMIDI<HardwareSerial5>, EuclidMidiSettings> midiDriver(
  (MIDI_NAMESPACE::SerialMIDI<HardwareSerial5>&)serialMIDI);

static void (*sysExAction)(const uint8_t*, uint16_t, bool) = nullptr;

// The library splits long SysEx as F0 ... F0, F7 ... F0, F7 ... F7, where the
// F7 and F0 at the joins are markers, not data.
static void receiveSysEx(byte* array, unsigned size)
{
    if (sysExAction == nullptr || size == 0)
        return;
    const uint8_t* p = array;
    if (p[0] == 0xF7)
    {
        p++;
        size--;
    }
    bool complete = size > 0 && p[size - 1] == 0xF7;
    if (!complete && size > 0 && p[size - 1] == 0xF0)
        size--;
    sysExAction(p, static_cast<uint16_t>(size), complete);
}

void UartMidiImpl::setReceiveSysExAction(void (*action)(const uint8_t* data, uint16_t length, bool complete))
{
    sysExAction = action;
    midiDriver.setHandleSystemExclusive(action != nullptr ? receiveSysEx : nullptr);
}

void UartMidiImpl::init()
{
    midiDriver.begin(0);
    midiDriver.turnThruOff();
}

void UartMidiImpl::setReceiveActions(MidiReceiveActions* receiveActions)
{
    midiDriver.setHandleNoteOn(receiveActions->noteOn);
    midiDriver.setHandleNoteOff(receiveActions->noteOff);
    midiDriver.setHandleClock(receiveActions->clock);
    midiDriver.setHandleStart(receiveActions->start);
    midiDriver.setHandleStop(receiveActions->stop);
    midiDriver.setHandleSongPosition(receiveActions->songPosition);
    midiDriver.setHandleContinue(receiveActions->continueMessage);
    midiDriver.setHandleControlChange(receiveActions->controlChange);
}

bool UartMidiImpl::isReadAvailable()
{
    return Serial5.available();
}

void UartMidiImpl::read()
{
    midiDriver.read();
}

void UartMidiImpl::sendRealTime(MessageType messageType)
{
    midiDriver.sendRealTime((midi::MidiType)messageType);
}

void UartMidiImpl::sendControlChange(uint8_t index, uint8_t value, uint8_t channel)
{
    midiDriver.sendControlChange(index, value, channel);
}

void UartMidiImpl::sendProgramChange(uint8_t programNumber, uint8_t channel)
{
    midiDriver.sendProgramChange(programNumber, channel);
}

void UartMidiImpl::sendAfterTouch(uint8_t velocity, uint8_t channel)
{
    midiDriver.sendAfterTouch(velocity, channel);
}

void UartMidiImpl::sendPolyPressure(uint8_t note, uint8_t value, uint8_t channel)
{
    midiDriver.sendPolyPressure(note, value, channel);
}

void UartMidiImpl::sendPitchBend(int value, uint8_t channel)
{
    midiDriver.sendPitchBend(value, channel);
}

void UartMidiImpl::sendNoteOn(uint8_t note, uint8_t velocity, uint8_t channel)
{
    midiDriver.sendNoteOn(note, velocity, channel);
}

void UartMidiImpl::sendNoteOff(uint8_t note, uint8_t velocity, uint8_t channel)
{
    midiDriver.sendNoteOff(note, velocity, channel);
}

void UartMidiImpl::sendSysEx(const uint8_t* data, uint32_t length)
{
    midiDriver.sendSysEx(length, data, true); // the data includes F0 and F7
}

void UartMidiImpl::setReceiveNoteOnAction(void (*action)(uint8_t, uint8_t, uint8_t))
{
    midiDriver.setHandleNoteOn(action);
}
