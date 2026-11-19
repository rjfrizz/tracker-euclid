#include "sysexQueue.h"

#include <string.h>

namespace euclid
{

bool SysexQueue::push(uint32_t key, const uint8_t* data, size_t length, uint8_t ports, uint16_t pauseMs)
{
    if (data == nullptr || length == 0 || length > static_cast<size_t>(kMaxLength))
        return false;

    lock();
    // A newer value for a parameter still waiting replaces it in place.
    for (int i = 0; i < count; i++)
    {
        Entry& e = entries[(head + i) % kCapacity];
        if (e.key == key && e.ports == ports)
        {
            memcpy(e.data, data, length);
            e.length = static_cast<uint8_t>(length);
            e.pauseMs = pauseMs;
            unlock();
            return true;
        }
    }
    if (count >= kCapacity)
    {
        unlock();
        return false;
    }
    Entry& e = entries[(head + count) % kCapacity];
    e.key = key;
    e.ports = ports;
    e.pauseMs = pauseMs;
    e.length = static_cast<uint8_t>(length);
    memcpy(e.data, data, length);
    count++;
    unlock();
    return true;
}

void SysexQueue::pump(uint32_t nowUs, void (*send)(const uint8_t* data, uint32_t length, uint8_t ports))
{
    if (count == 0)
        return;
    if (timing && static_cast<int32_t>(nowUs - nextUs) < 0)
        return;

    Entry& e = entries[head];
    if (send)
        send(e.data, e.length, e.ports);
    nextUs = nowUs + static_cast<uint32_t>(e.pauseMs) * 1000;
    timing = true;
    head = (head + 1) % kCapacity;
    count--;
}

int SysexQueue::waiting() const
{
    return count;
}

} // namespace euclid
