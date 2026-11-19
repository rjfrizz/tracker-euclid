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
//   request F0 43 d+20 03 F7        asks the synth for its patch (up to 4)
//   dump vced F0 43 d+00 03 00 5D [ data ] F7
//                                   a reply holding vced params' values
//   name vced 77 10                 the patch name: 10 bytes at vced 77
//
// A param line gives its template, its number (decimal or 0x hex: the
// parameter number, or a Roland address as printed), its name and its
// range. show= is added to the value for display, send= to the value sent,
// and init= is the value before anything is known about the patch.
//
// Template words: a hex byte; d+XX (device number - 1, plus XX); p (the
// number's low 7 bits); ph / pl (bits 7-13 / 0-6); a2 a1 a0 (the number's
// bytes 2, 1 and 0: Roland addresses as the manuals print them, so 0x040010
// is 04 00 10); v, vh, vl (the value: low 7 bits, bits 7-13, bits 0-6); and
// [ ... ], which emits a Roland checksum of the bytes inside after them.
//
// A request uses only hex bytes, d+XX and [ ]. A dump is matched against
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
constexpr int kMaxRequests = 4;
constexpr int kMaxDumps = 8;

enum class Token : uint8_t
{
    literal,
    device,
    paramLow7, // p
    paramHigh, // ph
    paramLow,  // pl
    address2,
    address1,
    address0,
    value7, // v
    valueHigh,
    valueLow,
    sumStart, // [
    checksum, // ]
    data,     // dumps: the values
};

struct TemplateToken
{
    Token type;
    uint8_t value; // literal byte, or the base for `device`
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
};

struct Dump
{
    uint8_t templ; // whose params it holds
    bool hasAddress;
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
    uint8_t requestCount;
    Template requests[kMaxRequests];
    uint8_t dumpCount;
    Dump dumps[kMaxDumps];
    uint8_t nameTempl; // 0xFF for none
    uint32_t nameNumber;
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
size_t buildMessage(const Definition& d, uint16_t param, int value, uint8_t device, uint8_t* out,
                    size_t maxLength);

// Request message i (0 .. requestCount-1). Returns its length, or 0.
size_t buildRequest(const Definition& d, uint8_t request, uint8_t device, uint8_t* out, size_t maxLength);

// An incoming message recognised as one of the definition's dumps.
struct DumpData
{
    uint8_t dump;
    const uint8_t* data; // points into the message
    size_t length;
    uint32_t address; // of data[0], for dumps with a2 a1 a0
};

// Whether message (F0 ... F7) is one of d's dumps from that device.
bool matchDump(const Definition& d, const uint8_t* message, size_t length, uint8_t device, DumpData& out);

// Sets the values of the params the dump holds; returns how many.
int applyDump(const Definition& d, const DumpData& dump, int16_t* values);

// The patch name, if this dump holds it. False otherwise.
bool dumpName(const Definition& d, const DumpData& dump, char* out, size_t length);

// The value as shown: a list name, or the number plus `show`.
void formatValue(const Definition& d, uint16_t param, int value, char* out, size_t length);

// The Roland checksum of bytes: 128 minus their sum, mod 128.
uint8_t rolandChecksum(const uint8_t* bytes, size_t count);

} // namespace synth
} // namespace euclid
