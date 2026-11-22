#pragma once

#include <stddef.h>
#include <stdint.h>

// Synth definitions: what a hardware synth's parameters are and how to set
// them over SysEx, read from a text file on the SD card (Synths/*.syn), so
// a new synth needs no firmware change.
//
// File format, one item per line ('#' starts a comment):
//   synth "Yamaha TX81Z"            name shown on screen
//   pause 30                        ms to wait after each message
//   device 1                        default device number, 1-16
//   port din                        default port: din, usb, both or notes
//   template vced F0 43 d+10 12 p v F7
//   list waves "W1" "W2" "W3"       named values for params (list=waves)
//   group "Op 4 EG"                 params below belong to this group
//   param vced 0 "Attack" 0 31 [init=31] [show=1] [list=waves] [send=64]
//                              [stride=16] [reload=1] [nibbles=2] [then=2]
//   listen                          take its dumps whenever they come (a synth
//                                   that sends its patch itself, unasked)
//   parts 8 246 [channel=2]         8 copies of the params, 246 apart;
//                                   part 1 listens on MIDI channel 2...
//                                   (a distance of 0: only params with their
//                                   own stride= move with the part)
//   request F0 43 d+20 03 F7        asks the synth for its patch (up to 8)
//   request addr=0x040000 F0 41 d+10 16 11 [ a2 a1 a0 00 01 76 ] F7
//                                   a request with an address (and stride=)
//   request each F0 18 04 d+00 02 pl ph F7
//                                   asked once for each param (its number in
//                                   p, pl, ph): for synths that answer one
//                                   setting at a time (an E-mu Proteus)
//   dump vced F0 43 d+00 03 00 5D [ data ] F7
//                                   a reply holding vced params' values
//                                   (dump a+b ...: those of templates a and b;
//                                   dump vced packed ...: data in DSI's
//                                   packing, 7 bytes in 8 with their top bits
//                                   first, unpacked to whole bytes; dump vced
//                                   hex ...: each byte as two ASCII hex
//                                   digits, high first, as Yamaha's TX802
//                                   performances come; a dump
//                                   with p, pl, ph holds that one param, its
//                                   data a 14-bit signed value, low 7 bits
//                                   first)
//   name vced 77 10 [stride=128]    the patch name: 10 bytes at vced 77
//                                   (stride=: its distance between parts)
//   hint "Memory picks a voice"     a reminder the Synth page shows of how
//                                   to change patches on this synth
//   map p2inst 0x0000 0x0101 ...    the numbers a param's values send
//                                   (map=p2inst: value n sends the n-th)
//
// A param line gives its template, its number (decimal or 0x hex: the
// parameter number, or a Roland address as printed), its name and its
// range. show= is added to the value for display, send= to the value sent,
// and init= is the value before anything is known about the patch.
// stride= gives the param its own distance between parts (see parts), and
// reload=1 asks for the patch again after it changes (a tone selection).
// at=n is the param's byte in dumps when that isn't its number (a DSI
// Tetra's NRPN numbers and dump order differ).
// then=n also sends the param n lines further on (or back, if negative)
// after this one: for synths that act on a setting only when another
// arrives (a JV-1080 part takes its patch group with the patch number).
//
// A template can send several messages, split by | (each F0 ... F7, or a
// channel message such as d+C0 7F, a program change on the device's
// channel). Template words (also a3 for 4-byte addresses, and n1 n0 for a
// value sent as two 4-bit nibbles): a hex byte; d+XX (device number - 1, plus XX); p (the
// number's low 7 bits); ph / pl (bits 7-13 / 0-6); a2 a1 a0 (the number's
// bytes 2, 1 and 0: Roland addresses as the manuals print them, so 0x040010
// is 04 00 10); v, vh, vl (the value: low 7 bits, bits 7-13, bits 0-6); and
// [ ... ], which emits a Roland checksum of the bytes inside after them.
// v+1, n1+2, n0+2 (or with -): those of the param that many lines on (or
// back), so one message can carry several settings the synth wants
// together (a JV-1080 part's patch group, group ID and patch number).
//
// With parts (a multitimbral synth's parts, each with its own copy of the
// params), each channel picks one, and numbers and addresses move by
// (part - 1) * the distance (in a Roland address's 7-bit steps, for
// messages with a2 a1 a0).
//
// A request uses hex bytes, d+XX, [ ], and a2 a1 a0 for its addr=. A dump is matched against
// incoming SysEx: its hex bytes and d+XX must match, [ ] checks the
// checksum, a2 a1 a0 read the address of the first data byte, and `data`
// is the values, one byte each. A param of the dump's template takes the
// byte at its number (minus the address for a dump with one), less send=.
//
// Platform independent, so it's tested on a host (core/tests/euclid/).

namespace euclid
{
namespace synth
{

constexpr int kMaxParams = 640;
constexpr int kMaxGroups = 96;
constexpr int kMaxTemplates = 12;
constexpr int kMaxTokens = 32;
constexpr int kMaxLists = 64;
constexpr int kStringPool = 12288;
constexpr int kMaxMessage = 32;
constexpr int kMaxRequests = 8;
constexpr int kMaxDumps = 8;
constexpr int kMaxMaps = 4;
constexpr int kMapPool = 512; // numbers, all maps together

enum class Token : uint8_t
{
    literal,
    device,
    paramLow7, // p
    paramHigh, // ph
    paramLow,  // pl
    address3, // a3: 4-byte Roland addresses (the JV-1080)
    address2,
    address1,
    address0,
    value7, // v
    valueHigh,
    valueLow,
    nibbleHigh, // n1: the value's bits 4-7, for values sent as two nibbles
    nibbleLow,  // n0: bits 0-3
    otherValue,      // v+k: another param's value (k in the token's offset)
    otherNibbleHigh, // n1+k
    otherNibbleLow,  // n0+k
    sumStart, // [
    checksum, // ]
    data,     // dumps: the values
    split,    // | between the messages of a template
};

struct TemplateToken
{
    Token type;
    uint8_t value; // literal byte, or the base for `device`
    int8_t offset; // v+k, n1+k, n0+k: k
};

struct Template
{
    char name[12];
    uint8_t count;
    TemplateToken tokens[kMaxTokens];
};

struct Param
{
    uint16_t name;  // offset into the string pool
    uint8_t group;
    uint8_t templ;
    uint32_t number;
    int16_t min;
    int16_t max;
    int16_t show;
    int16_t send;
    int16_t init;
    uint8_t list; // 0xFF for none
    uint16_t stride; // between parts; 0 for the definition's
    bool reload;
    uint8_t map; // 0xFF for none: the value is sent as it is
    uint8_t nibbles; // 0: a byte in dumps; 2: two bytes of 4 bits, high first
    int8_t then;     // also send the param this many further on (0: none)
    uint16_t at;     // its place in dumps, if not its number (0xFFFF: none)
};

struct Dump
{
    uint8_t templ;      // whose params it holds (the first, for the name)
    uint16_t templMask; // bit per template: all whose params it holds
    bool hasAddress;
    bool packed; // DSI's 7-in-8 packing
    bool hex;    // each byte as two ASCII hex digits
    bool byNumber; // holds one param, named by p/pl/ph, with a 14-bit value
    Template pattern;
};

struct Group
{
    uint16_t name;
    uint16_t first; // index of its first param
    uint16_t count;
};

struct List
{
    uint16_t first; // offset of its first string
    uint16_t count; // strings follow one another in the pool
};

struct Definition
{
    char name[32];
    char hint[64]; // empty for none
    uint16_t pauseMs;
    uint8_t device; // default, 1-16
    uint8_t ports;  // default: 1 DIN, 2 USB, 3 both, 0 wherever notes go

    uint16_t paramCount;
    Param params[kMaxParams];
    uint8_t groupCount;
    Group groups[kMaxGroups];
    uint8_t templateCount;
    Template templates[kMaxTemplates];
    uint8_t listCount;
    List lists[kMaxLists];
    char listNames[kMaxLists][12];
    bool listen;       // dumps are taken whenever they arrive, not just when asked
    uint8_t partCount; // 0: no parts
    uint16_t partStride;
    uint8_t partChannel; // the MIDI channel part 1 listens on, 0 if none
    bool hasEach; // a request sent once for each param
    Template eachRequest;
    uint8_t requestCount;
    Template requests[kMaxRequests];
    uint32_t requestAddress[kMaxRequests];
    uint16_t requestStride[kMaxRequests]; // 0 for the definition's
    uint8_t dumpCount;
    Dump dumps[kMaxDumps];
    uint8_t mapCount;
    char mapNames[kMaxMaps][12];
    uint16_t mapFirst[kMaxMaps];
    uint16_t mapLength[kMaxMaps];
    uint16_t mapUsed;
    uint16_t mapPool[kMapPool];
    uint8_t nameTempl; // 0xFF for none
    uint32_t nameNumber;
    uint16_t nameStride; // between parts; 0 for the definition's
    uint8_t nameLength;

    uint16_t poolUsed;
    char pool[kStringPool];

    // When parsing fails: the line and what was wrong.
    uint16_t errorLine;
    char error[48];

    const char* str(uint16_t offset) const { return pool + offset; }
};

// Parses a definition. On failure returns false with errorLine/error set.
bool parse(const char* text, size_t length, Definition& out);

// The SysEx message that sets a param to value, for a device number 1-16.
// Returns its length, or 0 if it doesn't fit.
// part is 1 .. partCount (anything for a definition without parts).
// values: every param's value (for v+k and the like; without them, their
// init= values go).
size_t buildMessage(const Definition& d, uint16_t param, int value, uint8_t device, uint8_t part, uint8_t* out,
                    size_t maxLength, const int16_t* values = nullptr);

// The each request (hasEach) for one param. Returns its length, or 0.
size_t buildEachRequest(const Definition& d, uint16_t param, uint8_t device, uint8_t part, uint8_t* out,
                        size_t maxLength);

// Request message i (0 .. requestCount-1). Returns its length, or 0.
size_t buildRequest(const Definition& d, uint8_t request, uint8_t device, uint8_t part, uint8_t* out,
                    size_t maxLength);

// An incoming message recognised as one of the definition's dumps.
constexpr size_t kMaxUnpacked = 512;

struct DumpData
{
    uint8_t dump;
    const uint8_t* data; // points into the message (or, packed, at unpacked)
    size_t length;
    uint32_t address; // of data[0], for dumps with a2 a1 a0
    const uint8_t* packedData; // a packed dump's data in the message
    size_t packedLength;
    uint8_t unpacked[kMaxUnpacked]; // don't copy a DumpData: data may point here
};

// Whether message (F0 ... F7) is one of d's dumps from that device.
bool matchDump(const Definition& d, const uint8_t* message, size_t length, uint8_t device, DumpData& out);

// Sets the values of the params the dump holds; returns how many.
int applyDump(const Definition& d, const DumpData& dump, uint8_t part, int16_t* values);

// The patch name, if this dump holds it. False otherwise.
bool dumpName(const Definition& d, const DumpData& dump, uint8_t part, char* out, size_t length);

// Patches on the card are dumps as the synth sent them. These rewrite one:
// fillDump puts values into it (for the params it holds), and moveDump
// readdresses it from one device and part to another; both mend its
// checksums. False if the message isn't one of d's dumps from fromDevice.
bool fillDump(const Definition& d, const int16_t* values, uint8_t part, uint8_t device, uint8_t* message,
              size_t length);
bool moveDump(const Definition& d, uint8_t* message, size_t length, uint8_t fromDevice, uint8_t fromPart,
              uint8_t toDevice, uint8_t toPart);
// Whether a dump holds a selection (a reload= param, such as which tone a
// part plays): those go to the synth before the patch they select.
bool dumpHasSelection(const Definition& d, const DumpData& dump, uint8_t part);

// The value as shown: a list name, or the number plus `show`.
void formatValue(const Definition& d, uint16_t param, int value, char* out, size_t length);

// The length of the first MIDI message in bytes (SysEx to its F7, or a
// channel message), 0 if there isn't a whole one.
size_t messageLength(const uint8_t* bytes, size_t count);

// The Roland checksum of bytes: 128 minus their sum, mod 128.
uint8_t rolandChecksum(const uint8_t* bytes, size_t count);

} // namespace synth
} // namespace euclid
