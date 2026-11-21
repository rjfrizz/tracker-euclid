// Connects the platform-independent Euclidean engine to the Tracker's MIDI
// output, random source, interrupt guard and SD card.

#include "euclidPlatform.h"

#include "Platform.h"
#include "TrackerImpl.h"
#include "config.h"
#include "euclidEngine.h"
#include "euclidChain.h"
#include "euclidSets.h"
#include "fileManager.h"
#include "log.h"
#include "mtMidi.h"
#include "mtAudioEngine.h"
#include "mtSequencer.h"
#include "util_math.h"

#include <cr_section_macros.h>
#include <stdio.h>
#include <string.h>

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

// Sampler output: the channel's audio track plays a project instrument
// (a sample one), as the stock sequencer does for a step: velocity is the
// step volume, 0-100.
AUDIO_CORE_RAM void sampleOn(uint8_t ch, uint8_t instrument, uint8_t note, uint8_t velocity)
{
    if (ch >= kSampleTracks || !isSampleInstrument(instrument) || sequencer.isRenderMode())
        return;
    const int8_t volume = static_cast<int8_t>(velocity * 100 / 127);
    instrumentPlayer[ch].noteOn(instrument, static_cast<int8_t>(note), volume, 0, 0, 0, 0);
}

AUDIO_CORE_RAM void sampleOff(uint8_t ch)
{
    if (ch < kSampleTracks)
        instrumentPlayer[ch].noteOff();
}

void controlChange(uint8_t cc, uint8_t value, uint8_t channel)
{
    sendCC(cc, value, channel, false);
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
constexpr const char* kCurrentScenesPath = "Sets/current.scn";
constexpr const char* kCurrentScenesTempPath = "Sets/currscn.tmp";
constexpr uint32_t kAutosaveDelayMs = 2000;

bool bootLoaded = false;
uint8_t slotOfState = 0;
uint32_t changeCountAtSlot = 0;
uint32_t changeCountSaved = 0;
uint32_t lastSeenChangeCount = 0;
uint32_t lastChangeMs = 0;

// Set files in and out, one at a time (too big for the stack).
__NOINIT(EXTERNAL_RAM) uint8_t setBuffer[euclid::kSetFileSize];

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

    uint8_t* buf = setBuffer; // one at a time, from the main loop
    auto [isOpen, file] = f->openFile(path);
    if (!isOpen)
        return false;
    // Older versions are smaller; deserializeSet checks the exact size.
    size_t size = f->getFileSize(file);
    int32_t got = size <= euclid::kSetFileSize ? f->readFile(buf, size, file) : -1;
    f->closeFile(file);

    if (got != static_cast<int32_t>(size))
        return false;
    return euclid::deserializeSet(euclidEngine, buf, size, slotOut);
}

//------------------------------------------------------------------------------
// Scenes. File layout (version 1):
//   'E' 'U' 'S' 'N', version (1), scene count (4), 2 reserved
//   per scene: length (2 bytes, little-endian; 0 = empty), then a set image
//              of that length (euclid::serializeSet, with its own version
//              and CRC, so older images still load after a format change)
//   CRC-32 of everything before it
constexpr uint8_t kScenesVersion = 1;
constexpr size_t kScenesFileMax = 8 + euclidScenes::kScenes * (2 + euclid::kSetFileSize) + 4;

__NOINIT(EXTERNAL_RAM) uint8_t sceneImage[euclidScenes::kScenes][euclid::kSetFileSize];
uint16_t sceneLength[euclidScenes::kScenes] = {};
bool scenesDirty = false;

// While a scene plays, what was playing before it (the Params key goes back
// to it). Autosave keeps this, not the scene, as the working state.
int8_t activeScene = -1;
__NOINIT(EXTERNAL_RAM) uint8_t homeImage[euclid::kSetFileSize];
uint16_t homeLength = 0;
__NOINIT(EXTERNAL_RAM) uint8_t scenesFile[kScenesFileMax];

size_t buildScenesFile()
{
    uint8_t* p = scenesFile;
    p[0] = 'E';
    p[1] = 'U';
    p[2] = 'S';
    p[3] = 'N';
    p[4] = kScenesVersion;
    p[5] = euclidScenes::kScenes;
    p[6] = 0;
    p[7] = 0;
    size_t n = 8;
    for (uint8_t i = 0; i < euclidScenes::kScenes; i++)
    {
        scenesFile[n++] = static_cast<uint8_t>(sceneLength[i]);
        scenesFile[n++] = static_cast<uint8_t>(sceneLength[i] >> 8);
        memcpy(scenesFile + n, sceneImage[i], sceneLength[i]);
        n += sceneLength[i];
    }
    uint32_t crc = euclid::crc32(scenesFile, n);
    for (int b = 0; b < 4; b++)
        scenesFile[n++] = static_cast<uint8_t>(crc >> (8 * b));
    return n;
}

void clearScenes()
{
    for (uint8_t i = 0; i < euclidScenes::kScenes; i++)
        sceneLength[i] = 0;
}

// Reads a scenes file; a missing or damaged one leaves every scene empty.
void readScenesFile(const char* path)
{
    clearScenes();
    FileSystem* f = fs();
    if (!f->exists(path))
        return;
    auto [isOpen, file] = f->openFile(path);
    if (!isOpen)
        return;
    size_t size = f->getFileSize(file);
    int32_t got = size <= kScenesFileMax ? f->readFile(scenesFile, size, file) : -1;
    f->closeFile(file);
    if (got != static_cast<int32_t>(size) || size < 12)
        return;

    uint32_t crc = 0;
    for (int b = 0; b < 4; b++)
        crc |= static_cast<uint32_t>(scenesFile[size - 4 + b]) << (8 * b);
    if (crc != euclid::crc32(scenesFile, size - 4))
        return;
    if (scenesFile[0] != 'E' || scenesFile[1] != 'U' || scenesFile[2] != 'S' || scenesFile[3] != 'N' ||
        scenesFile[4] != kScenesVersion)
        return;

    size_t n = 8;
    for (uint8_t i = 0; i < euclidScenes::kScenes && i < scenesFile[5]; i++)
    {
        if (n + 2 > size - 4)
            break;
        uint16_t len = static_cast<uint16_t>(scenesFile[n] | (scenesFile[n + 1] << 8));
        n += 2;
        if (len > euclid::kSetFileSize || n + len > size - 4)
            break;
        memcpy(sceneImage[i], scenesFile + n, len);
        sceneLength[i] = len;
        n += len;
    }
}

bool writeScenesFile(const char* path, const char* tempPath);

bool saveCurrent()
{
    uint8_t* buf = setBuffer; // one at a time, from the main loop
    uint32_t count = euclidEngine.getChangeCount();
    if (activeScene >= 0 && homeLength == euclid::kSetFileSize)
        memcpy(buf, homeImage, euclid::kSetFileSize);
    else if (euclid::serializeSet(euclidEngine, slotOfState, buf, euclid::kSetFileSize) == 0)
        return false;
    if (!writeFileSafely(kCurrentPath, kCurrentTempPath, buf, euclid::kSetFileSize))
    {
        LOG("euclid: autosave failed");
        return false;
    }
    changeCountSaved = count;
    if (scenesDirty && writeScenesFile(kCurrentScenesPath, kCurrentScenesTempPath))
        scenesDirty = false;
    return true;
}

bool writeScenesFile(const char* path, const char* tempPath)
{
    size_t n = buildScenesFile();
    return writeFileSafely(path, tempPath, scenesFile, n);
}

void scenesPath(uint8_t slot, char* out, size_t len, bool temp)
{
    snprintf(out, len, "%s/set%d.%s", kSetsDir, slot, temp ? "sct" : "scn");
}
} // namespace

void euclidRoutePlay()
{
    // There are no stock patterns to hear: the sequencer only keeps time.
    sequencer.setPatternSilenced(true);
    euclidEngine.setOutputEnabled(true);
}

void euclidPlatformInit()
{
    euclid::Output out;
    out.noteOn = noteOn;
    out.noteOff = noteOff;
    out.sampleOn = sampleOn;
    out.sampleOff = sampleOff;
    out.controlChange = controlChange;
    out.randomPercent = randomPercent;
    out.randomBelow = randomBelow;
    out.lock = lock;
    out.unlock = unlock;
    euclidEngine.init(out);
    euclidSynth::init();
    euclid::loadDemoDefaults(euclidEngine);
}

//------------------------------------------------------------------------------
// The chain (Song key): steps of a set and a scene for some bars.

namespace
{
constexpr const char* kChainPath = "Sets/chain.chn";
constexpr const char* kChainTempPath = "Sets/chain.tmp";
euclid::Chain chainData;
bool chainDirty = false;
uint32_t chainChangedMs = 0;
int8_t chainIssued = -1; // the step last sent to the engine

// A step's set and scene, which the engine plays from the next launch point
// (straight away while stopped).
void applyChainStep(int i)
{
    const euclid::ChainStep& s = chainData.steps[i];
    if (s.set != 0 && cardReady())
        euclidSets::load(s.set);
    if (s.scene != 0)
        euclidScenes::recall(static_cast<uint8_t>(s.scene - 1));
    else if (s.set == 0)
        euclidScenes::returnHome();
}

void updateChain()
{
    if (!chainData.on)
    {
        chainIssued = -1;
        return;
    }
    // Stopped, the first step is ready to go.
    if (!euclidEngine.isRunning())
    {
        if (chainIssued != 0)
        {
            applyChainStep(0);
            chainIssued = 0;
        }
        return;
    }
    const uint32_t bar = euclid::kMicroStepsPerBar;
    const uint32_t now = euclidEngine.getPlayTime();
    if (euclid::chainStepAt(chainData, now / bar) < 0)
    {
        sequencer.stop(); // the end of a chain that doesn't loop
        return;
    }
    // Each step goes to the engine a launch period early, so it lands on
    // the step's first bar.
    const int ahead =
      euclid::chainStepAt(chainData, (now + euclid::launchToMicroSteps(euclidEngine.getLaunch())) / bar);
    if (ahead >= 0 && ahead != chainIssued)
    {
        applyChainStep(ahead);
        chainIssued = static_cast<int8_t>(ahead);
    }
}

void saveChain()
{
    uint8_t buf[euclid::kChainFileSize];
    if (euclid::serializeChain(chainData, buf, sizeof(buf)) > 0 &&
        writeFileSafely(kChainPath, kChainTempPath, buf, sizeof(buf)))
        chainDirty = false;
}

void loadChain()
{
    FileSystem* f = fs();
    if (f == nullptr || !f->exists(kChainPath))
        return;
    auto [isOpen, file] = f->openFile(kChainPath);
    if (!isOpen)
        return;
    uint8_t buf[euclid::kChainFileSize];
    const size_t size = f->getFileSize(file);
    const int32_t got = size == sizeof(buf) ? f->readFile(buf, size, file) : -1;
    f->closeFile(file);
    if (got == static_cast<int32_t>(sizeof(buf)))
        euclid::deserializeChain(chainData, buf, sizeof(buf));
}
} // namespace

namespace euclidChain
{
euclid::Chain& get()
{
    return chainData;
}

void changed()
{
    chainDirty = true;
    chainChangedMs = nowMs();
}

int8_t playingStep(uint32_t* barInStep, uint32_t* stepBars)
{
    if (!chainData.on || !euclidEngine.isRunning())
        return -1;
    uint32_t start = 0;
    const uint32_t bar = euclidEngine.getPlayTime() / euclid::kMicroStepsPerBar;
    const int i = euclid::chainStepAt(chainData, bar, &start);
    if (i >= 0)
    {
        if (barInStep != nullptr)
            *barInStep = bar - start;
        if (stepBars != nullptr)
            *stepBars = chainData.steps[i].bars;
    }
    return static_cast<int8_t>(i);
}
} // namespace euclidChain

void euclidPlatformUpdate()
{
    euclidSynth::update();

    if (!bootLoaded)
        return;

    updateChain();
    if (chainDirty && nowMs() - chainChangedMs >= kAutosaveDelayMs && cardReady())
        saveChain();

    uint32_t count = euclidEngine.getChangeCount();
    if (count != lastSeenChangeCount)
    {
        lastSeenChangeCount = count;
        lastChangeMs = nowMs();
    }

    if ((count != changeCountSaved || scenesDirty) && nowMs() - lastChangeMs >= kAutosaveDelayMs && cardReady())
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
        readScenesFile(kCurrentScenesPath);
        loadChain();
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

    uint8_t* buf = setBuffer; // one at a time, from the main loop
    if (euclid::serializeSet(euclidEngine, slot, buf, euclid::kSetFileSize) == 0)
        return false;
    if (!writeFileSafely(path, temp, buf, euclid::kSetFileSize))
        return false;

    char scn[32];
    char scnTemp[32];
    scenesPath(slot, scn, sizeof(scn), false);
    scenesPath(slot, scnTemp, sizeof(scnTemp), true);
    writeScenesFile(scn, scnTemp);

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

    char scn[32];
    scenesPath(slot, scn, sizeof(scn), false);
    readScenesFile(scn);
    scenesDirty = true; // the working copy's scenes are now these
    activeScene = -1;   // a loaded set is the new home

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

namespace euclidScenes
{
bool save(uint8_t scene)
{
    if (scene >= kScenes)
        return false;
    size_t n = euclid::serializeSet(euclidEngine, 0, sceneImage[scene], sizeof(sceneImage[scene]));
    sceneLength[scene] = static_cast<uint16_t>(n);
    scenesDirty = n > 0;
    lastChangeMs = nowMs();
    return n > 0;
}

namespace
{
// A scene or home image is the music; the Global page's settings for how
// things launch, freeze and layer stay as they are.
bool loadImage(const uint8_t* image, size_t length)
{
    const euclid::Launch launch = euclidEngine.getLaunch();
    const euclid::FreezeLength freezeLength = euclidEngine.getFreezeLength();
    const bool layer = euclidEngine.getLayer();
    bool ok = euclid::deserializeSet(euclidEngine, image, length);
    euclidEngine.setLaunch(launch);
    euclidEngine.setFreezeLength(freezeLength);
    euclidEngine.setLayer(layer);
    return ok;
}
} // namespace

bool recall(uint8_t scene)
{
    if (scene >= kScenes || sceneLength[scene] == 0)
        return false;

    // Leaving home: keep it to come back to.
    if (activeScene < 0)
    {
        size_t n = euclid::serializeSet(euclidEngine, slotOfState, homeImage, sizeof(homeImage));
        homeLength = static_cast<uint16_t>(n);
        if (n == 0)
            return false;
    }
    if (!loadImage(sceneImage[scene], sceneLength[scene]))
        return false;
    activeScene = static_cast<int8_t>(scene);
    return true;
}

bool keepAsHome()
{
    if (activeScene < 0)
        return false;
    activeScene = -1; // the old home is let go; autosave keeps this now
    lastChangeMs = nowMs();
    changeCountSaved = 0; // save it soon
    return true;
}

bool returnHome()
{
    if (activeScene < 0)
        return false;
    activeScene = -1;
    return homeLength > 0 && loadImage(homeImage, homeLength);
}

int8_t active()
{
    return activeScene;
}

bool has(uint8_t scene)
{
    return scene < kScenes && sceneLength[scene] > 0;
}
} // namespace euclidScenes
