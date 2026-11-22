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
    uint8_t part = 1; // for synths with parts; 0 follows the MIDI channel
};
Channel channels[euclid::kChannels];

char fileNames[euclidSynth::kMaxFiles][euclidSynth::kNameLength];
uint8_t fileCount = 0;
bool scanned = false;
char lastError[64] = "";

euclid::SysexQueue queue;

// Sending a whole patch, a few messages at a time as the queue empties.
int8_t sendAllChannel = -1;
uint32_t sendAllNext = 0; // over two passes of the params

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

// Each channel's patch as the synth last sent it (or as last loaded), at
// device 1 and part 1: the patch library saves these with the page's values
// written in.
constexpr uint8_t kImages = 4;
constexpr size_t kImageMax = 512;
__NOINIT(EXTERNAL_RAM) uint8_t images[euclid::kChannels][kImages][kImageMax];
uint16_t imageLength[euclid::kChannels][kImages] = {};
uint8_t imageCount[euclid::kChannels] = {};

// Whole dumps to send (a patch from the card), one message at a time once
// the queue is empty, each given time to go out.
constexpr size_t kBulkMax = 2048;
__NOINIT(EXTERNAL_RAM) uint8_t bulk[kBulkMax];
volatile uint16_t bulkLength = 0;
volatile uint16_t bulkSent = 0;
uint8_t bulkPorts = 0;
uint16_t bulkPauseMs = 0;
uint32_t bulkNextUs = 0;

// The patches in the channel's synth's folder (names without .syx).
constexpr uint8_t kMaxPatches = 64;
char patchNames[kMaxPatches][euclidSynth::kNameLength];
uint8_t patchCount = 0;

// A patch request waiting for its reply.
int8_t requestChannel = -1;
uint32_t requestDeadline = 0;
// A request each (one per param), fed to the queue as it empties.
int8_t eachChannel = -1;
uint16_t eachNext = 0;
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

// Keeps a received dump as the channel's patch, at device 1 and part 1,
// replacing one of the same kind and address.
void keepImage(uint8_t ch, const uint8_t* msg, size_t len, uint8_t device, uint8_t part)
{
    const Definition* d = euclidSynth::definition(ch);
    if (d == nullptr || len > kImageMax)
        return;
    uint8_t temp[kImageMax];
    memcpy(temp, msg, len);
    static euclid::synth::DumpData mine, other; // main loop only; big (unpacking room)
    if (!euclid::synth::moveDump(*d, temp, len, device, part, 1, 1) || !euclid::synth::matchDump(*d, temp, len, 1, mine))
        return;
    if (d->dumps[mine.dump].byNumber)
        return; // one setting's reply: not a patch to keep
    uint8_t slot = imageCount[ch];
    for (uint8_t i = 0; i < imageCount[ch]; i++)
        if (euclid::synth::matchDump(*d, images[ch][i], imageLength[ch][i], 1, other) && other.dump == mine.dump &&
            other.address == mine.address)
            slot = i;
    if (slot >= kImages)
        return;
    memcpy(images[ch][slot], temp, len);
    imageLength[ch][slot] = static_cast<uint16_t>(len);
    if (slot == imageCount[ch])
        imageCount[ch]++;
}

void resetValues(uint8_t ch)
{
    imageCount[ch] = 0;
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
    const uint8_t part = euclidSynth::part(ch);
    if (part == 0)
        return; // the notes' MIDI channel isn't one of the synth's parts
    size_t n = euclid::synth::buildMessage(d, param, values[ch][param], c.device, part, msg, sizeof(msg), values[ch]);
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
    // A whole dump going out has the port to itself until it's had time to.
    if (bulkSent < bulkLength && static_cast<int32_t>(nowUs - bulkNextUs) < 0)
        return;
    queue.pump(nowUs, send);
    if (bulkSent >= bulkLength || queue.waiting() > 0)
        return;
    const size_t len = euclid::synth::messageLength(bulk + bulkSent, bulkLength - bulkSent);
    if (len == 0)
    {
        bulkSent = bulkLength;
        return;
    }
    send(bulk + bulkSent, static_cast<uint32_t>(len), bulkPorts);
    // 320 us a byte at MIDI's speed, then the synth's pause.
    bulkNextUs = nowUs + static_cast<uint32_t>(len) * 320u + bulkPauseMs * 1000u;
    bulkSent = static_cast<uint16_t>(bulkSent + len);
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
    {
        snprintf(eventText, sizeof(eventText), "Got \"%s\"", patchName);
        // The patch names the channel (choosing a list name replaces it).
        euclidEngine.setTextName(requestChannel, patchName);
    }
    else
        snprintf(eventText, sizeof(eventText), "Got the patch");
    requestChannel = -1;
    eachChannel = -1;
    events++;
}

// Replies to a request: the values go straight onto the page.
void takeInbox()
{
    while (inboxTail != inboxHead)
    {
        const uint8_t slot = inboxTail;
        if (requestChannel < 0)
        {
            // Synths that send their patch unasked (when a patch is chosen
            // on them): it's taken by the channels using them.
            for (uint8_t ch = 0; ch < euclid::kChannels; ch++)
            {
                const Definition* d = definition(ch);
                euclid::synth::DumpData dump;
                if (d == nullptr || !d->listen ||
                    !euclid::synth::matchDump(*d, inbox[slot], inboxLength[slot], channels[ch].device, dump))
                    continue;
                const uint8_t part = euclidSynth::part(ch);
                if (part == 0)
                    continue;
                euclid::synth::applyDump(*d, dump, part, values[ch]);
                char name[24] = "";
                euclid::synth::dumpName(*d, dump, part, name, sizeof(name));
                imageCount[ch] = 0;
                keepImage(ch, inbox[slot], inboxLength[slot], channels[ch].device, part);
                if (name[0] != 0)
                {
                    euclidEngine.setTextName(ch, name);
                    snprintf(eventText, sizeof(eventText), "Got \"%s\"", name);
                }
                else
                    snprintf(eventText, sizeof(eventText), "Got the patch");
                events++;
            }
        }
        else
        {
            const uint8_t ch = static_cast<uint8_t>(requestChannel);
            const Definition* d = definition(ch);
            euclid::synth::DumpData dump;
            if (d != nullptr &&
                euclid::synth::matchDump(*d, inbox[slot], inboxLength[slot], channels[ch].device, dump))
            {
                const uint8_t part = euclidSynth::part(ch);
                valuesReceived += euclid::synth::applyDump(*d, dump, part, values[ch]);
                euclid::synth::dumpName(*d, dump, part, patchName, sizeof(patchName));
                keepImage(ch, inbox[slot], inboxLength[slot], channels[ch].device, part);
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
    // A request each: the next params' requests, as the queue has room.
    if (eachChannel >= 0)
    {
        const Channel& c = channels[eachChannel];
        const Definition* d = c.slot >= 0 ? &definitions[c.slot] : nullptr;
        const uint8_t part = euclidSynth::part(static_cast<uint8_t>(eachChannel));
        while (d != nullptr && eachNext < d->paramCount && queue.waiting() < euclid::SysexQueue::kCapacity - 4)
        {
            uint8_t msg[euclid::synth::kMaxMessage];
            const uint16_t p = eachNext++;
            const size_t n = euclid::synth::buildEachRequest(*d, p, c.device, part, msg, sizeof(msg));
            if (n > 0)
                queue.push(0x80000000u | (static_cast<uint32_t>(eachChannel) << 16) | p, msg, n, c.ports, d->pauseMs);
        }
        if (d == nullptr || eachNext >= d->paramCount)
            eachChannel = -1;
    }
    if (requestChannel >= 0 && eachChannel < 0)
    {
        // The wait starts once the request has gone out.
        if (requestDeadline == 0 && queue.waiting() == 0)
            requestDeadline = nowMs() + kReplyWaitMs;
        // (Not while requests are still waiting to go: replies to the
        // first can come before the last are sent.)
        if (requestDeadline != 0 && queue.waiting() == 0 && static_cast<int32_t>(nowMs() - requestDeadline) >= 0)
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
    // Two passes: first the selections (reload= params: which stored voice
    // or patch plays), which load a patch, then everything else on top, so
    // the synth ends up as the page is (as a project saved it).
    const uint32_t total = 2u * d.paramCount;
    while (sendAllNext < total && queue.waiting() < euclid::SysexQueue::kCapacity - 4)
    {
        const uint32_t i = sendAllNext++;
        const bool selections = i < d.paramCount;
        const uint16_t p = static_cast<uint16_t>(selections ? i : i - d.paramCount);
        if (d.params[p].reload == selections)
            queueParam(static_cast<uint8_t>(sendAllChannel), p);
    }
    if (sendAllNext >= total)
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
        // Parts follow the notes' MIDI channel where the synth says how, or
        // else start as the channel's own number.
        const Definition& def = definitions[slot];
        c.part = def.partChannel > 0 ? 0
                 : (def.partCount > 0 ? static_cast<uint8_t>(ch < def.partCount ? ch + 1 : 1) : 1);
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
    if (ch >= euclid::kChannels)
        return 1;
    const Definition* d = definition(ch);
    if (channels[ch].part != 0 || d == nullptr)
        return channels[ch].part != 0 ? channels[ch].part : 1;
    if (d->partChannel == 0)
        return 1;
    // Following the MIDI channel the channel's notes go out on.
    const int p = euclidEngine.getLiveExpression(ch).midiChannel - d->partChannel + 1;
    return p >= 1 && p <= d->partCount ? static_cast<uint8_t>(p) : 0;
}

uint8_t partSetting(uint8_t ch)
{
    return ch < euclid::kChannels ? channels[ch].part : 1;
}

void setPart(uint8_t ch, uint8_t p)
{
    const Definition* d = definition(ch);
    const uint8_t lowest = d != nullptr && d->partChannel > 0 ? 0 : 1;
    if (d != nullptr && p >= lowest && p <= (d->partCount > 0 ? d->partCount : 1))
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
    if (p.then != 0)
        queueParam(ch, static_cast<uint16_t>(param + p.then));
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
    return sendAllChannel >= 0 || queue.waiting() > 0 || bulkSent < bulkLength;
}

bool requestPatch(uint8_t ch)
{
    const Definition* d = definition(ch);
    if (d == nullptr)
        return false;
    if (d->requestCount == 0 && !d->hasEach)
    {
        if (d->listen)
            snprintf(lastError, sizeof(lastError), "Pick a patch on the %s: it sends it", d->name);
        else
            snprintf(lastError, sizeof(lastError), "%s can't send its patch", d->name);
        return false;
    }
    const uint8_t part = euclidSynth::part(ch);
    if (part == 0)
    {
        snprintf(lastError, sizeof(lastError), "MIDI ch %d isn't a %s part", euclidEngine.getLiveExpression(ch).midiChannel,
                 d->name);
        return false;
    }
    if (requestChannel >= 0 && requestChannel != ch)
        finishRequest();
    const Channel& c = channels[ch];
    for (uint8_t i = 0; i < d->requestCount; i++)
    {
        uint8_t msg[euclid::synth::kMaxMessage];
        size_t n = euclid::synth::buildRequest(*d, i, c.device, part, msg, sizeof(msg));
        if (n > 0)
            queue.push((static_cast<uint32_t>(ch) << 16) | (0xFF00u + i), msg, n, c.ports, d->pauseMs);
    }
    requestChannel = static_cast<int8_t>(ch);
    eachChannel = d->hasEach ? static_cast<int8_t>(ch) : -1;
    eachNext = 0;
    imageCount[ch] = 0; // the reply is the patch now
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

//------------------------------------------------------------------------------
// The patch library: Synths/<synth>/<name>.syx, the dumps as the synth sends
// them (device 1, part 1), with the page's values written in.

namespace
{
// Synths/TX81Z for TX81Z.syn.
bool patchDir(uint8_t ch, char* out, size_t length)
{
    if (ch >= euclid::kChannels || channels[ch].slot < 0)
        return false;
    snprintf(out, length, "%s/%s", kSynthsDir, slots[channels[ch].slot].file);
    char* dot = strrchr(out, '.');
    if (dot != nullptr)
        *dot = 0;
    return true;
}
} // namespace

uint8_t scanPatches(uint8_t ch)
{
    patchCount = 0;
    char dir[64];
    FileSystem* f = fs();
    if (!patchDir(ch, dir, sizeof(dir)) || f == nullptr || !f->isSdCardInserted() || !f->exists(dir))
        return 0;
    static char names[kMaxPatches * kNameLength];
    static char* list[kMaxPatches];
    int count = kMaxPatches;
    f->listDirContents(
      dir, FileSystem::DirContentType::kFiles,
      [](const char* name) {
          size_t len = strlen(name);
          return len > 4 && len < kNameLength && strcasecmp(name + len - 4, ".syx") == 0;
      },
      names, sizeof(names), kNameLength, count, list);
    for (int i = 0; i < count && patchCount < kMaxPatches; i++)
    {
        snprintf(patchNames[patchCount], kNameLength, "%s", list[i]);
        patchNames[patchCount][strlen(patchNames[patchCount]) - 4] = 0; // without .syx
        patchCount++;
    }
    for (int a = 1; a < patchCount; a++)
        for (int b = a; b > 0 && strcasecmp(patchNames[b], patchNames[b - 1]) < 0; b--)
        {
            char t[kNameLength];
            memcpy(t, patchNames[b], kNameLength);
            memcpy(patchNames[b], patchNames[b - 1], kNameLength);
            memcpy(patchNames[b - 1], t, kNameLength);
        }
    return patchCount;
}

uint8_t patches()
{
    return patchCount;
}

const char* patch(uint8_t index)
{
    return index < patchCount ? patchNames[index] : "";
}

bool savePatch(uint8_t ch, char* savedAs, size_t length)
{
    const Definition* d = definition(ch);
    char dir[64];
    FileSystem* f = fs();
    if (d == nullptr || !patchDir(ch, dir, sizeof(dir)))
        return false;
    if (imageCount[ch] == 0)
    {
        snprintf(lastError, sizeof(lastError), "Get the patch first: Shift + Insert");
        return false;
    }
    if (f == nullptr || !f->isSdCardInserted())
    {
        snprintf(lastError, sizeof(lastError), "No SD card");
        return false;
    }

    // The dumps with the page's values written in.
    size_t n = 0;
    for (uint8_t i = 0; i < imageCount[ch] && n + imageLength[ch][i] <= kFileMax; i++)
    {
        memcpy(fileBuffer + n, images[ch][i], imageLength[ch][i]);
        euclid::synth::fillDump(*d, values[ch], 1, 1, reinterpret_cast<uint8_t*>(fileBuffer) + n,
                                imageLength[ch][i]);
        n += imageLength[ch][i];
    }

    // Named after the channel (a patch's name, usually), made safe for the
    // card, with a number added if that name is taken.
    char base[16];
    const char* name = euclidEngine.getNameShown(ch);
    size_t b = 0;
    for (const char* c = name != nullptr ? name : "Patch"; *c != 0 && b + 1 < sizeof(base); c++)
    {
        const char k = *c;
        const bool ok = (k >= 'A' && k <= 'Z') || (k >= 'a' && k <= 'z') || (k >= '0' && k <= '9') || k == ' ' ||
                        k == '-' || k == '_';
        base[b++] = ok ? k : '_';
    }
    while (b > 0 && base[b - 1] == ' ')
        b--;
    base[b] = 0;
    if (b == 0)
        snprintf(base, sizeof(base), "Patch");

    if (!f->exists(kSynthsDir))
        f->mkdir(0, kSynthsDir);
    if (!f->exists(dir))
        f->mkdir(0, dir);
    char path[96];
    char file[24];
    for (int k = 1; k < 100; k++)
    {
        if (k == 1)
            snprintf(file, sizeof(file), "%s", base);
        else
            snprintf(file, sizeof(file), "%s %d", base, k);
        snprintf(path, sizeof(path), "%s/%s.syx", dir, file);
        if (!f->exists(path))
            break;
    }
    auto [isOpen, handle] = f->openFile(path, FileSystem::FileOpenMode::kWriteCreateNew);
    if (!isOpen)
    {
        snprintf(lastError, sizeof(lastError), "Can't write %s", file);
        return false;
    }
    const int32_t wrote = f->writeFile(fileBuffer, n, handle);
    f->closeFile(handle);
    if (wrote != static_cast<int32_t>(n))
    {
        snprintf(lastError, sizeof(lastError), "Card full?");
        return false;
    }
    snprintf(savedAs, length, "%s", file);
    scanPatches(ch);
    return true;
}

bool loadPatch(uint8_t ch, uint8_t index)
{
    const Definition* d = definition(ch);
    char dir[64];
    FileSystem* f = fs();
    if (d == nullptr || index >= patchCount || !patchDir(ch, dir, sizeof(dir)) || f == nullptr)
        return false;
    const uint8_t part = euclidSynth::part(ch);
    if (part == 0)
    {
        snprintf(lastError, sizeof(lastError), "MIDI ch %d isn't a %s part", euclidEngine.getLiveExpression(ch).midiChannel,
                 d->name);
        return false;
    }
    if (bulkSent < bulkLength)
    {
        snprintf(lastError, sizeof(lastError), "Still sending the last one");
        return false;
    }

    char path[96];
    snprintf(path, sizeof(path), "%s/%s.syx", dir, patchNames[index]);
    auto [isOpen, handle] = f->openFile(path);
    if (!isOpen)
    {
        snprintf(lastError, sizeof(lastError), "Can't open %s", patchNames[index]);
        return false;
    }
    size_t size = f->getFileSize(handle);
    int32_t got = size <= kBulkMax ? f->readFile(fileBuffer, size, handle) : -1;
    f->closeFile(handle);
    if (got != static_cast<int32_t>(size))
    {
        snprintf(lastError, sizeof(lastError), "%s is too big", patchNames[index]);
        return false;
    }

    // Every dump in the file: kept as the channel's patch, read into the
    // page, and moved to the channel's device and part to send. Selections
    // (a part's tone) go first, or they'd load over the patch.
    const Channel& c = channels[ch];
    uint8_t* src = reinterpret_cast<uint8_t*>(fileBuffer);
    size_t out = 0;
    int dumps = 0;
    char name[24] = "";
    imageCount[ch] = 0;
    for (int pass = 0; pass < 2; pass++)
    {
        size_t at = 0;
        while (at < size)
        {
            size_t len = euclid::synth::messageLength(src + at, size - at);
            if (len == 0)
                break;
            uint8_t* msg = src + at;
            at += len;
            euclid::synth::DumpData dump;
            if (len > kImageMax || !euclid::synth::matchDump(*d, msg, len, 1, dump) ||
                euclid::synth::dumpHasSelection(*d, dump, 1) != (pass == 0) || out + len > kBulkMax)
                continue;
            keepImage(ch, msg, len, 1, 1);
            memcpy(bulk + out, msg, len);
            euclid::synth::moveDump(*d, bulk + out, len, 1, 1, c.device, part);
            if (euclid::synth::matchDump(*d, bulk + out, len, c.device, dump))
            {
                euclid::synth::applyDump(*d, dump, part, values[ch]);
                euclid::synth::dumpName(*d, dump, part, name, sizeof(name));
            }
            out += len;
            dumps++;
        }
    }
    if (dumps == 0)
    {
        snprintf(lastError, sizeof(lastError), "Not a %s patch", d->name);
        return false;
    }
    euclidEngine.setTextName(ch, name[0] != 0 ? name : patchNames[index]);
    bulkPorts = c.ports;
    bulkPauseMs = d->pauseMs;
    bulkSent = 0;
    bulkLength = static_cast<uint16_t>(out); // last: the audio callback starts on this
    return true;
}

bool deletePatch(uint8_t ch, uint8_t index)
{
    char dir[64];
    char path[96];
    FileSystem* f = fs();
    if (index >= patchCount || !patchDir(ch, dir, sizeof(dir)) || f == nullptr)
        return false;
    snprintf(path, sizeof(path), "%s/%s.syx", dir, patchNames[index]);
    const bool ok = f->remove(path);
    scanPatches(ch);
    return ok;
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

void clearAssignments()
{
    for (uint8_t ch = 0; ch < euclid::kChannels; ch++)
    {
        channels[ch].slot = -1;
        imageCount[ch] = 0;
    }
}

// File layout (version 1): 'E' 'U' 'S' 'Y', version, channels, 2 reserved;
// then per channel: its definition file's name (kNameLength bytes, empty
// for none), the value count (2 bytes) and the values (2 bytes each), the
// patch count (1 byte) and each patch's length (2 bytes) and bytes. Numbers
// little-endian.
constexpr uint8_t kStateVersion = 1;

bool saveState(const char* path)
{
    FileSystem* f = fs();
    if (f == nullptr || !f->isSdCardInserted())
        return false;
    auto [isOpen, handle] = f->openFile(path, FileSystem::FileOpenMode::kWriteCreateNew);
    if (!isOpen)
        return false;
    bool ok = true;
    auto put = [&](const void* data, size_t len) {
        if (ok && len > 0)
            ok = f->writeFile(data, len, handle) == static_cast<int32_t>(len);
    };
    const uint8_t header[8] = { 'E', 'U', 'S', 'Y', kStateVersion, euclid::kChannels, 0, 0 };
    put(header, sizeof(header));
    for (uint8_t ch = 0; ch < euclid::kChannels; ch++)
    {
        const Channel& c = channels[ch];
        char name[kNameLength] = {};
        uint16_t count = 0;
        if (c.slot >= 0)
        {
            strncpy(name, slots[c.slot].file, kNameLength - 1);
            count = definitions[c.slot].paramCount;
        }
        put(name, sizeof(name));
        const uint8_t n[2] = { static_cast<uint8_t>(count), static_cast<uint8_t>(count >> 8) };
        put(n, 2);
        for (uint16_t i = 0; i < count; i++)
        {
            const uint8_t v[2] = { static_cast<uint8_t>(values[ch][i]), static_cast<uint8_t>(values[ch][i] >> 8) };
            put(v, 2);
        }
        const uint8_t images_ = c.slot >= 0 ? imageCount[ch] : 0;
        put(&images_, 1);
        for (uint8_t i = 0; i < images_; i++)
        {
            const uint8_t l[2] = { static_cast<uint8_t>(imageLength[ch][i]), static_cast<uint8_t>(imageLength[ch][i] >> 8) };
            put(l, 2);
            put(images[ch][i], imageLength[ch][i]);
        }
    }
    f->closeFile(handle);
    return ok;
}

// After loadAssignments: a channel's values come back only if it edits the
// same synth (and the same number of settings) as when saved.
bool loadState(const char* path)
{
    FileSystem* f = fs();
    if (f == nullptr || !f->isSdCardInserted() || !f->exists(path))
        return false;
    auto [isOpen, handle] = f->openFile(path);
    if (!isOpen)
        return false;
    const size_t size = f->getFileSize(handle);
    const int32_t got = size <= kFileMax ? f->readFile(fileBuffer, size, handle) : -1;
    f->closeFile(handle);
    if (got != static_cast<int32_t>(size) || size < 8)
        return false;
    const uint8_t* p = reinterpret_cast<const uint8_t*>(fileBuffer);
    if (p[0] != 'E' || p[1] != 'U' || p[2] != 'S' || p[3] != 'Y' || p[4] != kStateVersion)
        return false;
    size_t at = 8;
    auto have = [&](size_t n) { return at + n <= size; };
    for (uint8_t ch = 0; ch < euclid::kChannels && ch < p[5]; ch++)
    {
        if (!have(kNameLength + 2))
            return false;
        char name[kNameLength];
        memcpy(name, p + at, kNameLength);
        name[kNameLength - 1] = 0;
        at += kNameLength;
        const uint16_t count = static_cast<uint16_t>(p[at] | (p[at + 1] << 8));
        at += 2;
        if (!have(2u * count + 1))
            return false;
        const Channel& c = channels[ch];
        const bool same = c.slot >= 0 && strcmp(slots[c.slot].file, name) == 0 &&
                          definitions[c.slot].paramCount == count;
        if (same)
            for (uint16_t i = 0; i < count; i++)
                values[ch][i] = static_cast<int16_t>(p[at + 2 * i] | (p[at + 2 * i + 1] << 8));
        at += 2u * count;
        const uint8_t n = p[at++];
        if (same)
            imageCount[ch] = 0;
        for (uint8_t i = 0; i < n; i++)
        {
            if (!have(2))
                return false;
            const uint16_t len = static_cast<uint16_t>(p[at] | (p[at + 1] << 8));
            at += 2;
            if (!have(len))
                return false;
            if (same && i < kImages && len <= kImageMax)
            {
                memcpy(images[ch][i], p + at, len);
                imageLength[ch][i] = len;
                imageCount[ch] = static_cast<uint8_t>(i + 1);
            }
            at += len;
        }
    }
    return true;
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
            if (!number(prtNo))
                prtNo = -1; // files from before parts
        }
        if (ok && ch >= 1 && ch <= euclid::kChannels)
        {
            for (int i = 0; i < fileCount; i++)
                if (strcmp(fileNames[i], name) == 0 && assign(static_cast<uint8_t>(ch - 1), i))
                {
                    setDevice(static_cast<uint8_t>(ch - 1), static_cast<uint8_t>(dev));
                    setPorts(static_cast<uint8_t>(ch - 1), static_cast<uint8_t>(prt));
                    if (prtNo >= 0)
                        setPart(static_cast<uint8_t>(ch - 1), static_cast<uint8_t>(prtNo));
                }
        }
        line = next;
    }
}
} // namespace euclidSynth
