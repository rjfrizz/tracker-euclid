// Connects the platform-independent Euclidean engine to the Tracker's MIDI
// output, random source, interrupt guard and SD card.

#include "euclidPlatform.h"

#include "Platform.h"
#include "TrackerImpl.h"
#include "config.h"
#include "euclidEngine.h"
#include "euclidSets.h"
#include "fileManager.h"
#include "log.h"
#include "mtMidi.h"
#include "util_math.h"

#include <stdio.h>

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

//------------------------------------------------------------------------------
// Sets

constexpr const char* kSetsDir = "Sets";
constexpr const char* kCurrentPath = "Sets/current.eus";
constexpr const char* kCurrentTempPath = "Sets/current.tmp";
constexpr uint32_t kAutosaveDelayMs = 2000;

bool bootLoaded = false;
uint8_t slotOfState = 0;
uint32_t changeCountAtSlot = 0;
uint32_t changeCountSaved = 0;
uint32_t lastSeenChangeCount = 0;
uint32_t lastChangeMs = 0;

FileSystem* fs()
{
    return TrackerImpl::getInstance()->getPlatform()->getFileSystem();
}

uint32_t nowMs()
{
    return TrackerImpl::getInstance()->getPlatform()->getTimer()->millis();
}

void slotPath(uint8_t slot, char* out, size_t len)
{
    snprintf(out, len, "%s/set%d.eus", kSetsDir, slot);
}

bool cardReady()
{
    FileSystem* f = fs();
    return f != nullptr && f->isSdCardInserted() && newFileManager.getStatus() == fmIdle;
}

bool writeFile(const char* path, const uint8_t* data, size_t len)
{
    FileSystem* f = fs();
    if (!f->exists(kSetsDir))
        f->mkdir(0, kSetsDir);

    auto [isOpen, file] = f->openFile(path, FileSystem::FileOpenMode::kWriteCreateNew);
    if (!isOpen)
        return false;
    int32_t written = f->writeFile(data, len, file);
    f->closeFile(file);
    return written == static_cast<int32_t>(len);
}

// Write to a temporary file first, so a power cut mid-write can't leave a
// truncated set where the good one was.
bool writeFileSafely(const char* path, const char* tempPath, const uint8_t* data, size_t len)
{
    FileSystem* f = fs();
    if (!writeFile(tempPath, data, len))
        return false;
    f->remove(path);
    return f->rename(tempPath, path);
}

bool readSetFile(const char* path, uint8_t* slotOut)
{
    FileSystem* f = fs();
    if (!f->exists(path))
        return false;

    uint8_t buf[euclid::kSetFileSize];
    auto [isOpen, file] = f->openFile(path);
    if (!isOpen)
        return false;
    size_t size = f->getFileSize(file);
    int32_t got = size == sizeof(buf) ? f->readFile(buf, sizeof(buf), file) : -1;
    f->closeFile(file);

    if (got != static_cast<int32_t>(sizeof(buf)))
        return false;
    return euclid::deserializeSet(euclidEngine, buf, sizeof(buf), slotOut);
}

bool saveCurrent()
{
    uint8_t buf[euclid::kSetFileSize];
    uint32_t count = euclidEngine.getChangeCount();
    if (euclid::serializeSet(euclidEngine, slotOfState, buf, sizeof(buf)) == 0)
        return false;
    if (!writeFileSafely(kCurrentPath, kCurrentTempPath, buf, sizeof(buf)))
    {
        LOG("euclid: autosave failed");
        return false;
    }
    changeCountSaved = count;
    return true;
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

void euclidPlatformUpdate()
{
    if (!bootLoaded)
        return;

    uint32_t count = euclidEngine.getChangeCount();
    if (count != lastSeenChangeCount)
    {
        lastSeenChangeCount = count;
        lastChangeMs = nowMs();
    }

    if (count != changeCountSaved && nowMs() - lastChangeMs >= kAutosaveDelayMs && cardReady())
    {
        saveCurrent();
    }
}

namespace euclidSets
{
void bootLoad()
{
    if (bootLoaded)
        return;

    FileSystem* f = fs();
    if (f != nullptr && f->isSdCardInserted())
    {
        uint8_t slot = 0;
        // A leftover temp file means power went off between writing it and
        // renaming it, so it's the newest complete copy.
        bool ok = readSetFile(kCurrentTempPath, &slot) || readSetFile(kCurrentPath, &slot);
        if (ok)
        {
            slotOfState = slot;
            LOG("euclid: restored working state");
        }
    }

    bootLoaded = true;
    changeCountSaved = euclidEngine.getChangeCount();
    changeCountAtSlot = changeCountSaved;
    lastSeenChangeCount = changeCountSaved;
}

bool save(uint8_t slot)
{
    if (slot < 1 || slot > euclid::kSetSlots || !cardReady())
        return false;

    char path[32];
    char temp[32];
    slotPath(slot, path, sizeof(path));
    snprintf(temp, sizeof(temp), "%s/set%d.tmp", kSetsDir, slot);

    uint8_t buf[euclid::kSetFileSize];
    if (euclid::serializeSet(euclidEngine, slot, buf, sizeof(buf)) == 0)
        return false;
    if (!writeFileSafely(path, temp, buf, sizeof(buf)))
        return false;

    slotOfState = slot;
    changeCountAtSlot = euclidEngine.getChangeCount();
    saveCurrent(); // so the working copy remembers which slot it is
    return true;
}

bool load(uint8_t slot)
{
    if (slot < 1 || slot > euclid::kSetSlots || !cardReady())
        return false;

    char path[32];
    slotPath(slot, path, sizeof(path));
    if (!readSetFile(path, nullptr))
        return false;

    slotOfState = slot;
    changeCountAtSlot = euclidEngine.getChangeCount();
    return true;
}

bool exists(uint8_t slot)
{
    if (slot < 1 || slot > euclid::kSetSlots)
        return false;
    FileSystem* f = fs();
    if (f == nullptr || !f->isSdCardInserted())
        return false;
    char path[32];
    slotPath(slot, path, sizeof(path));
    return f->exists(path);
}

uint8_t currentSlot()
{
    return slotOfState;
}

bool editedSinceSlot()
{
    return euclidEngine.getChangeCount() != changeCountAtSlot;
}
} // namespace euclidSets
