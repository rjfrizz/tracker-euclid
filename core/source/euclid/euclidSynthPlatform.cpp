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
    uint8_t part = 1; // for synths with parts
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

// Incoming SysEx. MIDI input is read in the audio callback, which puts whole
// messages in the inbox; the main loop takes them out.
constexpr size_t kInMax = 1024; // longest message kept
constexpr uint8_t kInbox = 4;
struct Assembly
{
    uint16_t length;
    bool active;
    bool overflow;
};
__NOINIT(EXTERNAL_RAM) uint8_t assemblyData[2][kInMax];
Assembly assembly[2] = {};
__NOINIT(EXTERNAL_RAM) uint8_t inbox[kInbox][kInMax];
uint16_t inboxLength[kInbox];
volatile uint8_t inboxHead = 0; // written by the audio callback
volatile uint8_t inboxTail = 0; // written by the main loop

// What came in, for "no reply" to say whether anything arrived at all.
volatile uint32_t heardCount = 0;
volatile uint16_t heardLength = 0;
volatile uint8_t heardPort = 0;
uint8_t heardStart[6];
uint32_t heardAtRequest = 0;
uint32_t jackAtRequest = 0;
uint32_t usbAtRequest = 0;

// A patch request waiting for its reply.
int8_t requestChannel = -1;
uint32_t requestDeadline = 0;
uint8_t dumpsReceived = 0;
int valuesReceived = 0;
char patchName[24] = "";
uint32_t events = 0;
char eventText[64] = "";
constexpr uint32_t kReplyWaitMs = 2500; // after the request goes out
constexpr uint32_t kReloadDelayMs = 500; // after the last change to a reload= param

// A tone selection changed: get the new patch once the encoder rests.
int8_t reloadChannel = -1;
uint32_t reloadAt = 0;
constexpr uint32_t kMoreWaitMs = 400;   // after a reply, for the rest

void receive(uint8_t port, const uint8_t* data, uint16_t length)
{
    Assembly& a = assembly[port];
    uint8_t* buf = assemblyData[port];
    for (uint16_t i = 0; i < length; i++)
    {
        const uint8_t b = data[i];
        if (b == 0xF0)
        {
            a.active = true;
            a.overflow = false;
            a.length = 0;
        }
        if (!a.active)
            continue;
        if (a.length < kInMax)
            buf[a.length++] = b;
        else
            a.overflow = true;
        if (b != 0xF7)
            continue;
        a.active = false;
        memcpy(heardStart, buf, sizeof(heardStart));
        heardLength = a.length;
        heardPort = port;
        heardCount = heardCount + 1;
        const uint8_t next = static_cast<uint8_t>((inboxHead + 1) % kInbox);
        if (a.overflow || next == inboxTail)
            continue; // too long, or the main loop is behind: dropped
        memcpy(inbox[inboxHead], buf, a.length);
        inboxLength[inboxHead] = a.length;
        inboxHead = next;
    }
}

uint32_t nowMs()
{
    return TrackerImpl::getInstance()->getPlatform()->getTimer()->millis();
}

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
    size_t n = euclid::synth::buildMessage(d, param, values[ch][param], c.device, c.part, msg, sizeof(msg));
    // One entry a message, each with its pause (a template can send several).
    size_t at = 0;
    for (uint32_t piece = 0; at < n; piece++)
    {
        size_t len = euclid::synth::messageLength(msg + at, n - at);
        if (len == 0)
            break;
        queue.push((piece << 24) | (static_cast<uint32_t>(ch) << 16) | param, msg + at, len, c.ports, d.pauseMs);
        at += len;
    }
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

void receiveDin(const uint8_t* data, uint16_t length, bool complete)
{
    (void)complete; // the F7 says so
    receive(0, data, length);
}

void receiveUsb(const uint8_t* data, uint16_t length, bool complete)
{
    (void)complete;
    receive(1, data, length);
}

namespace
{
void finishRequest()
{
    const uint32_t heard = heardCount - heardAtRequest;
    if (dumpsReceived == 0 && heard == 0)
    {
        // Short, to fit the title: what reached each input while waiting.
        const unsigned long din = midiJackReads() - jackAtRequest;
        const unsigned long usb = midiUsbReads() - usbAtRequest;
        if (usb > 0)
            snprintf(eventText, sizeof(eventText), "No reply. In: %lu, USB %lu", din, usb);
        else
            snprintf(eventText, sizeof(eventText), "No reply. DIN in: %lu", din);
    }
    else if (dumpsReceived == 0)
        snprintf(eventText, sizeof(eventText), "%s x%lu: %02X %02X %02X %02X %02X %02X",
                 heardPort == 0 ? "DIN" : "USB", static_cast<unsigned long>(heard), heardStart[0], heardStart[1],
                 heardStart[2], heardStart[3], heardStart[4], heardStart[5]);
    else if (patchName[0] != 0)
        snprintf(eventText, sizeof(eventText), "Got \"%s\"", patchName);
    else
        snprintf(eventText, sizeof(eventText), "Got the patch");
    requestChannel = -1;
    events++;
}

// Replies to a request: the values go straight onto the page.
void takeInbox()
{
    while (inboxTail != inboxHead)
    {
        const uint8_t slot = inboxTail;
        if (requestChannel >= 0)
        {
            const uint8_t ch = static_cast<uint8_t>(requestChannel);
            const Definition* d = definition(ch);
            euclid::synth::DumpData dump;
            if (d != nullptr &&
                euclid::synth::matchDump(*d, inbox[slot], inboxLength[slot], channels[ch].device, dump))
            {
                valuesReceived += euclid::synth::applyDump(*d, dump, channels[ch].part, values[ch]);
                euclid::synth::dumpName(*d, dump, channels[ch].part, patchName, sizeof(patchName));
                dumpsReceived++;
                requestDeadline = nowMs() + kMoreWaitMs;
            }
        }
        inboxTail = static_cast<uint8_t>((slot + 1) % kInbox);
    }
}
} // namespace

void update()
{
    takeInbox();
    if (reloadChannel >= 0 && queue.waiting() == 0 && static_cast<int32_t>(nowMs() - reloadAt) >= 0)
    {
        const uint8_t ch = static_cast<uint8_t>(reloadChannel);
        reloadChannel = -1;
        requestPatch(ch);
    }
    if (requestChannel >= 0)
    {
        // The wait starts once the request has gone out.
        if (requestDeadline == 0 && queue.waiting() == 0)
            requestDeadline = nowMs() + kReplyWaitMs;
        if (requestDeadline != 0 && static_cast<int32_t>(nowMs() - requestDeadline) >= 0)
            finishRequest();
    }

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
    // Selections (reload= params, such as which stored voice plays) aren't
    // part of the patch: sending one would load that voice instead.
    while (sendAllNext < d.paramCount && queue.waiting() < euclid::SysexQueue::kCapacity - 4)
    {
        const uint16_t p = sendAllNext++;
        if (!d.params[p].reload)
            queueParam(static_cast<uint8_t>(sendAllChannel), p);
    }
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
        // Parts default to the channel's own number, as far as they go.
        c.part = definitions[slot].partCount > 0
                   ? static_cast<uint8_t>(ch < definitions[slot].partCount ? ch + 1 : 1)
                   : 1;
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

uint8_t part(uint8_t ch)
{
    return ch < euclid::kChannels ? channels[ch].part : 1;
}

void setPart(uint8_t ch, uint8_t p)
{
    const Definition* d = definition(ch);
    if (d != nullptr && p >= 1 && p <= (d->partCount > 0 ? d->partCount : 1))
        channels[ch].part = p;
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
    if (p.reload && d->requestCount > 0)
    {
        reloadChannel = static_cast<int8_t>(ch);
        reloadAt = nowMs() + kReloadDelayMs;
    }
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

bool requestPatch(uint8_t ch)
{
    const Definition* d = definition(ch);
    if (d == nullptr)
        return false;
    if (d->requestCount == 0)
    {
        snprintf(lastError, sizeof(lastError), "%s can't send its patch", d->name);
        return false;
    }
    if (requestChannel >= 0 && requestChannel != ch)
        finishRequest();
    const Channel& c = channels[ch];
    for (uint8_t i = 0; i < d->requestCount; i++)
    {
        uint8_t msg[euclid::synth::kMaxMessage];
        size_t n = euclid::synth::buildRequest(*d, i, c.device, c.part, msg, sizeof(msg));
        if (n > 0)
            queue.push((static_cast<uint32_t>(ch) << 16) | (0xFF00u + i), msg, n, c.ports, d->pauseMs);
    }
    requestChannel = static_cast<int8_t>(ch);
    requestDeadline = 0;
    dumpsReceived = 0;
    valuesReceived = 0;
    patchName[0] = 0;
    inboxTail = inboxHead; // anything older isn't the reply
    heardAtRequest = heardCount;
    jackAtRequest = midiJackReads();
    usbAtRequest = midiUsbReads();
    return true;
}

bool receiving()
{
    return requestChannel >= 0;
}

uint32_t receiveEvents()
{
    return events;
}

const char* receiveMessage()
{
    return eventText;
}

const char* error()
{
    return lastError;
}

// Synths/assign.txt: one line per assigned channel, "channel file device
// ports part" (files from before parts have no part).
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
        n += snprintf(text + n, sizeof(text) - n, "%d %s %d %d %d\n", ch + 1, slots[channels[ch].slot].file,
                      channels[ch].device, channels[ch].ports, channels[ch].part);
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
        int ch = 0, dev = 1, prt = 0, prtNo = 0;
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
            number(prtNo);
        }
        if (ok && ch >= 1 && ch <= euclid::kChannels)
        {
            for (int i = 0; i < fileCount; i++)
                if (strcmp(fileNames[i], name) == 0 && assign(static_cast<uint8_t>(ch - 1), i))
                {
                    setDevice(static_cast<uint8_t>(ch - 1), static_cast<uint8_t>(dev));
                    setPorts(static_cast<uint8_t>(ch - 1), static_cast<uint8_t>(prt));
                    if (prtNo > 0)
                        setPart(static_cast<uint8_t>(ch - 1), static_cast<uint8_t>(prtNo));
                }
        }
        line = next;
    }
}
} // namespace euclidSynth
