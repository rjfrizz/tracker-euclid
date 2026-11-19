#pragma once

#include <stddef.h>
#include <stdint.h>

// Outgoing SysEx for the synth editor. The main loop adds messages; the
// audio callback, which also sends the engine's notes, sends them one at a
// time, so a note never lands in the middle of a SysEx message. Messages
// for the same parameter (`key`) replace each other while waiting, so
// turning a knob fast sends the latest value instead of a backlog, and each
// message waits its synth's pause before the next one goes.
//
// Platform independent, so it's tested on a host (core/tests/euclid/).

namespace euclid
{

class SysexQueue
{
  public:
    static constexpr int kCapacity = 48;
    static constexpr int kMaxLength = 32;

    void setLock(void (*lock)(), void (*unlock)())
    {
        lockFn = lock;
        unlockFn = unlock;
    }

    // Main loop. False if it doesn't fit (the queue is full or the message
    // too long).
    bool push(uint32_t key, const uint8_t* data, size_t length, uint8_t ports, uint16_t pauseMs);

    // Audio callback: sends the next message if its turn has come.
    void pump(uint32_t nowUs, void (*send)(const uint8_t* data, uint32_t length, uint8_t ports));

    int waiting() const;

  private:
    struct Entry
    {
        uint32_t key;
        uint8_t length;
        uint8_t ports;
        uint16_t pauseMs;
        uint8_t data[kMaxLength];
    };

    void lock() const
    {
        if (lockFn)
            lockFn();
    }
    void unlock() const
    {
        if (unlockFn)
            unlockFn();
    }

    Entry entries[kCapacity];
    int head = 0; // next to send
    int count = 0;
    uint32_t nextUs = 0;
    bool timing = false;
    void (*lockFn)() = nullptr;
    void (*unlockFn)() = nullptr;
};

} // namespace euclid
