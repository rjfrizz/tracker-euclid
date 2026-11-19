// The synth editor on the Tracker: definition files from Synths/ on the SD
// card, which synth each channel talks to, the values being edited, and the
// SysEx queue that the audio callback empties.

#include "euclidPlatform.h"

#include "Platform.h"
#include "TrackerImpl.h"
#include "config.h"
#include "euclidEngine.h"
#include "mtMidi.h"
#include "synthDef.h"
#include "sysexQueue.h"

#include <cr_section_macros.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

namespace
{
using euclid::synth::Definition;

constexpr const char* kSynthsDir = "Synths";
constexpr const char* kAssignPath = "Synths/assign.txt";
constexpr int kSlots = 4;               // different synths loaded at once
constexpr size_t kFileMax = 48 * 1024;  // largest definition file

struct Slot
{
    char file[euclidSynth::kNameLength];
    bool used;
};

__NOINIT(EXTERNAL_RAM) Definition definitions[kSlots];
Slot slots[kSlots] = {};

__NOINIT(EXTERNAL_RAM) char fileBuffer[kFileMax];
__NOINIT(EXTERNAL_RAM) int16_t values[euclid::kChannels][euclid::synth::kMaxParams];

struct Channel
{
    int8_t slot = -1;
    uint8_t device = 1;
    uint8_t ports = 0;
};
Channel channels[euclid::kChannels];

char fileNames[euclidSynth::kMaxFiles][euclidSynth::kNameLength];
uint8_t fileCount = 0;
bool scanned = false;
char lastError[64] = "";

euclid::SysexQueue queue;

// Sending a whole patch, a few messages at a time as the queue empties.
int8_t sendAllChannel = -1;
uint16_t sendAllNext = 0;

void lockIrq()
{
    DISABLE_GLOBAL_INTERRUPTS();
}

void unlockIrq()
{
    ENABLE_GLOBAL_INTERRUPTS();
}

FileSystem* fs()
{
    return TrackerImpl::getInstance()->getPlatform()->getFileSystem();
}

void send(const uint8_t* data, uint32_t length, uint8_t ports)
{
    sendSysExMessage(data, length, ports);
}

int findSlot(const char* file)
{
    for (int i = 0; i < kSlots; i++)
        if (slots[i].used && strcmp(slots[i].file, file) == 0)
            return i;
    return -1;
}

bool slotInUse(int slot)
{
    for (const Channel& c : channels)
        if (c.slot == slot)
            return true;
    return false;
}

// Loads a definition into a free slot (or finds it already loaded).
int loadDefinition(const char* file)
{
    int slot = findSlot(file);
    if (slot >= 0)
        return slot;

    for (int i = 0; i < kSlots && slot < 0; i++)
        if (!slots[i].used || !slotInUse(i))
            slot = i;
    if (slot < 0)
    {
        snprintf(lastError, sizeof(lastError), "Only %d different synths at once", kSlots);
        return -1;
    }

    char path[64];
    snprintf(path, sizeof(path), "%s/%s", kSynthsDir, file);
    FileSystem* f = fs();
    auto [isOpen, handle] = f->openFile(path);
    if (!isOpen)
    {
        snprintf(lastError, sizeof(lastError), "Can't open %s", file);
        return -1;
    }
    size_t size = f->getFileSize(handle);
    int32_t got = size <= kFileMax ? f->readFile(fileBuffer, size, handle) : -1;
    f->closeFile(handle);
    if (got != static_cast<int32_t>(size))
    {
        snprintf(lastError, sizeof(lastError), "%s is too big", file);
        return -1;
    }

    slots[slot].used = false;
    if (!euclid::synth::parse(fileBuffer, size, definitions[slot]))
    {
        snprintf(lastError, sizeof(lastError), "%s line %d: %s", file, definitions[slot].errorLine,
                 definitions[slot].error);
        return -1;
    }
    snprintf(slots[slot].file, sizeof(slots[slot].file), "%s", file);
    slots[slot].used = true;
    lastError[0] = 0;
    return slot;
}

void resetValues(uint8_t ch)
{
    const Channel& c = channels[ch];
    if (c.slot < 0)
        return;
    const Definition& d = definitions[c.slot];
    for (uint16_t i = 0; i < d.paramCount; i++)
        values[ch][i] = d.params[i].init;
}

void queueParam(uint8_t ch, uint16_t param)
{
    const Channel& c = channels[ch];
    if (c.slot < 0)
        return;
    const Definition& d = definitions[c.slot];
    uint8_t msg[euclid::synth::kMaxMessage];
    size_t n = euclid::synth::buildMessage(d, param, values[ch][param], c.device, msg, sizeof(msg));
    if (n > 0)
        queue.push((static_cast<uint32_t>(ch) << 16) | param, msg, n, c.ports, d.pauseMs);
}
} // namespace

namespace euclidSynth
{
void init()
{
    queue.setLock(lockIrq, unlockIrq);
}

void pump(uint32_t nowUs)
{
    queue.pump(nowUs, send);
}

void update()
{
    // A whole-patch send, fed to the queue as it empties.
    if (sendAllChannel < 0)
        return;
    const Channel& c = channels[sendAllChannel];
    if (c.slot < 0)
    {
        sendAllChannel = -1;
        return;
    }
    const Definition& d = definitions[c.slot];
    while (sendAllNext < d.paramCount && queue.waiting() < euclid::SysexQueue::kCapacity - 4)
        queueParam(static_cast<uint8_t>(sendAllChannel), sendAllNext++);
    if (sendAllNext >= d.paramCount)
        sendAllChannel = -1;
}

void scan()
{
    fileCount = 0;
    FileSystem* f = fs();
    if (f == nullptr || !f->isSdCardInserted() || !f->exists(kSynthsDir))
    {
        scanned = true;
        return;
    }

    static char names[kMaxFiles * kNameLength];
    static char* list[kMaxFiles];
    int count = kMaxFiles;
    f->listDirContents(
      kSynthsDir, FileSystem::DirContentType::kFiles,
      [](const char* name) {
          size_t len = strlen(name);
          return len > 4 && len < kNameLength && strcasecmp(name + len - 4, ".syn") == 0;
      },
      names, sizeof(names), kNameLength, count, list);

    for (int i = 0; i < count && fileCount < kMaxFiles; i++)
        snprintf(fileNames[fileCount++], kNameLength, "%s", list[i]);

    // Alphabetical, so the list is the same each time.
    for (int a = 1; a < fileCount; a++)
        for (int b = a; b > 0 && strcasecmp(fileNames[b], fileNames[b - 1]) < 0; b--)
        {
            char t[kNameLength];
            memcpy(t, fileNames[b], kNameLength);
            memcpy(fileNames[b], fileNames[b - 1], kNameLength);
            memcpy(fileNames[b - 1], t, kNameLength);
        }
    scanned = true;
}

uint8_t files()
{
    if (!scanned)
        scan();
    return fileCount;
}

const char* file(uint8_t index)
{
    return index < fileCount ? fileNames[index] : "";
}

bool assign(uint8_t ch, int fileIndex)
{
    if (ch >= euclid::kChannels)
        return false;
    Channel& c = channels[ch];
    if (fileIndex < 0 || fileIndex >= fileCount)
    {
        c.slot = -1;
        lastError[0] = 0;
        return true;
    }
    int8_t previous = c.slot;
    c.slot = -1; // so its slot counts as free while loading
    int slot = loadDefinition(fileNames[fileIndex]);
    if (slot < 0)
    {
        c.slot = previous;
        return false;
    }
    c.slot = static_cast<int8_t>(slot);
    if (previous != slot)
    {
        c.device = definitions[slot].device;
        c.ports = definitions[slot].ports;
        resetValues(ch);
    }
    return true;
}

int assignedFile(uint8_t ch)
{
    if (ch >= euclid::kChannels || channels[ch].slot < 0)
        return -1;
    for (int i = 0; i < fileCount; i++)
        if (strcmp(fileNames[i], slots[channels[ch].slot].file) == 0)
            return i;
    return -1;
}

const Definition* definition(uint8_t ch)
{
    if (ch >= euclid::kChannels || channels[ch].slot < 0)
        return nullptr;
    return &definitions[channels[ch].slot];
}

uint8_t device(uint8_t ch)
{
    return ch < euclid::kChannels ? channels[ch].device : 1;
}

void setDevice(uint8_t ch, uint8_t d)
{
    if (ch < euclid::kChannels && d >= 1 && d <= 16)
        channels[ch].device = d;
}

uint8_t ports(uint8_t ch)
{
    return ch < euclid::kChannels ? channels[ch].ports : 0;
}

void setPorts(uint8_t ch, uint8_t p)
{
    if (ch < euclid::kChannels && p <= 3)
        channels[ch].ports = p;
}

int16_t value(uint8_t ch, uint16_t param)
{
    const Definition* d = definition(ch);
    return d != nullptr && param < d->paramCount ? values[ch][param] : 0;
}

void setValue(uint8_t ch, uint16_t param, int v)
{
    const Definition* d = definition(ch);
    if (d == nullptr || param >= d->paramCount)
        return;
    const auto& p = d->params[param];
    if (v < p.min)
        v = p.min;
    if (v > p.max)
        v = p.max;
    values[ch][param] = static_cast<int16_t>(v);
    queueParam(ch, param);
}

void sendAll(uint8_t ch)
{
    if (definition(ch) == nullptr)
        return;
    sendAllChannel = static_cast<int8_t>(ch);
    sendAllNext = 0;
    update();
}

bool sending()
{
    return sendAllChannel >= 0 || queue.waiting() > 0;
}

const char* error()
{
    return lastError;
}

// Synths/assign.txt: one line per assigned channel, "channel file device ports".
void saveAssignments()
{
    FileSystem* f = fs();
    if (f == nullptr || !f->isSdCardInserted())
        return;
    if (!f->exists(kSynthsDir))
        f->mkdir(0, kSynthsDir);

    char text[euclid::kChannels * (kNameLength + 16) + 64];
    int n = snprintf(text, sizeof(text), "# EUCLID: which synth each channel edits\n");
    for (uint8_t ch = 0; ch < euclid::kChannels; ch++)
    {
        if (channels[ch].slot < 0)
            continue;
        n += snprintf(text + n, sizeof(text) - n, "%d %s %d %d\n", ch + 1, slots[channels[ch].slot].file,
                      channels[ch].device, channels[ch].ports);
    }
    auto [isOpen, handle] = f->openFile(kAssignPath, FileSystem::FileOpenMode::kWriteCreateNew);
    if (!isOpen)
        return;
    f->writeFile(text, static_cast<size_t>(n), handle);
    f->closeFile(handle);
}

void loadAssignments()
{
    scan();
    FileSystem* f = fs();
    if (f == nullptr || !f->isSdCardInserted() || !f->exists(kAssignPath))
        return;
    auto [isOpen, handle] = f->openFile(kAssignPath);
    if (!isOpen)
        return;
    char text[1024];
    size_t size = f->getFileSize(handle);
    if (size >= sizeof(text))
        size = sizeof(text) - 1;
    int32_t got = f->readFile(text, size, handle);
    f->closeFile(handle);
    if (got < 0)
        return;
    text[got] = 0;

    char* line = text;
    while (line && *line)
    {
        char* next = strchr(line, '\n');
        if (next)
            *next++ = 0;
        // "channel file device ports", read by hand (sscanf costs a lot of
        // flash for what it does here).
        int ch = 0, dev = 1, prt = 0;
        char name[kNameLength] = "";
        const char* q = line;
        auto number = [&q](int& out) {
            while (*q == ' ')
                q++;
            if (*q < '0' || *q > '9')
                return false;
            out = 0;
            while (*q >= '0' && *q <= '9')
                out = out * 10 + (*q++ - '0');
            return true;
        };
        bool ok = line[0] != '#' && number(ch);
        if (ok)
        {
            while (*q == ' ')
                q++;
            size_t n = 0;
            while (*q && *q != ' ' && *q != '\r' && n + 1 < sizeof(name))
                name[n++] = *q++;
            name[n] = 0;
            ok = n > 0;
            number(dev);
            number(prt);
        }
        if (ok && ch >= 1 && ch <= euclid::kChannels)
        {
            for (int i = 0; i < fileCount; i++)
                if (strcmp(fileNames[i], name) == 0 && assign(static_cast<uint8_t>(ch - 1), i))
                {
                    setDevice(static_cast<uint8_t>(ch - 1), static_cast<uint8_t>(dev));
                    setPorts(static_cast<uint8_t>(ch - 1), static_cast<uint8_t>(prt));
                }
        }
        line = next;
    }
}
} // namespace euclidSynth
