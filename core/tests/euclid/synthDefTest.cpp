// Host-side tests for synth definitions and the SysEx queue. Not part of the
// firmware build.
//   c++ -std=c++17 -I core/source core/tests/euclid/synthDefTest.cpp \
//       core/source/euclid/synthDef.cpp core/source/euclid/sysexQueue.cpp -o /tmp/synthDefTest

#include "euclid/synthDef.h"
#include "euclid/sysexQueue.h"

#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

using namespace euclid;
using namespace euclid::synth;

static int failures = 0;

#define CHECK(cond)                                                                                \
    do                                                                                             \
    {                                                                                              \
        if (!(cond))                                                                               \
        {                                                                                          \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                            \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static std::unique_ptr<Definition> load(const char* text)
{
    auto d = std::make_unique<Definition>();
    if (!parse(text, std::strlen(text), *d))
        std::printf("parse failed at line %d: %s\n", d->errorLine, d->error);
    return d;
}

static std::vector<uint8_t> message(const Definition& d, uint16_t param, int value, uint8_t device)
{
    uint8_t buf[kMaxMessage];
    size_t n = buildMessage(d, param, value, device, buf, sizeof(buf));
    return std::vector<uint8_t>(buf, buf + n);
}

static const char* kYamaha = R"(
# a TX81Z-style voice edit
synth "Yamaha TX81Z"
pause 30
port din
template vced F0 43 d+10 12 p v F7
template aced F0 43 d+10 13 p v F7
list waves "W1" "W2" "W3" "W4" "W5" "W6" "W7" "W8"
group "Op 4 EG"
param vced 0 "Attack" 0 31 init=31
param vced 1 "Decay 1" 0 31
group "Op 4 Osc"
param aced 3 "Wave" 0 7 list=waves
param vced 12 "Detune" 0 6 show=-3 init=3
)";

static void testYamaha()
{
    auto d = load(kYamaha);
    CHECK(std::strcmp(d->name, "Yamaha TX81Z") == 0);
    CHECK(d->pauseMs == 30 && d->ports == 1);
    CHECK(d->paramCount == 4 && d->groupCount == 2);
    CHECK(d->groups[1].first == 2 && d->groups[1].count == 2);
    CHECK(std::strcmp(d->str(d->groups[0].name), "Op 4 EG") == 0);
    CHECK(d->params[0].init == 31);

    CHECK((message(*d, 0, 31, 1) == std::vector<uint8_t>{0xF0, 0x43, 0x10, 0x12, 0x00, 0x1F, 0xF7}));
    // Device 3, the ACED group, clamped to the range.
    CHECK((message(*d, 2, 99, 3) == std::vector<uint8_t>{0xF0, 0x43, 0x12, 0x13, 0x03, 0x07, 0xF7}));

    char text[16];
    formatValue(*d, 2, 4, text, sizeof(text));
    CHECK(std::strcmp(text, "W5") == 0);
    formatValue(*d, 3, 0, text, sizeof(text));
    CHECK(std::strcmp(text, "-3") == 0);
}

static void testRolandChecksum()
{
    auto d = load(R"(
synth "Roland D-110"
template dt1 F0 41 d+10 16 12 [ a2 a1 a0 v ] F7
group "Common"
param dt1 0x040000 "Level" 0 127
param dt1 0x04007F "Last" 0 127
)");
    // 04 00 00 3F: sum 67, checksum 128 - 67 = 61.
    CHECK((message(*d, 0, 0x3F, 1) ==
           std::vector<uint8_t>{0xF0, 0x41, 0x10, 0x16, 0x12, 0x04, 0x00, 0x00, 0x3F, 0x3D, 0xF7}));
    // 04 00 7F 7F: sum 258, mod 128 = 2, checksum 126.
    CHECK((message(*d, 1, 0x7F, 1) ==
           std::vector<uint8_t>{0xF0, 0x41, 0x10, 0x16, 0x12, 0x04, 0x00, 0x7F, 0x7F, 0x7E, 0xF7}));
    CHECK(rolandChecksum(nullptr, 0) == 0);
}

static void testEmu14Bit()
{
    auto d = load(R"(
synth "E-mu Proteus/2"
template prm F0 18 04 d+00 03 pl ph vl vh F7
group "Layer 1"
param prm 300 "Tune" -64 63 send=64
)");
    // Param 300 = 44 + 2*128; value 10 + 64 = 74.
    CHECK((message(*d, 0, 10, 1) == std::vector<uint8_t>{0xF0, 0x18, 0x04, 0x00, 0x03, 44, 2, 74, 0, 0xF7}));
}

static void testErrors()
{
    Definition* d = new Definition();
    const char* bad1 = "synth \"X\"\ngroup \"G\"\nparam nope 0 \"A\" 0 1\n";
    CHECK(!parse(bad1, std::strlen(bad1), *d));
    CHECK(d->errorLine == 3);
    const char* bad2 = "template t F0 [ v F7\ngroup \"G\"\nparam t 0 \"A\" 0 1\n";
    CHECK(!parse(bad2, std::strlen(bad2), *d));
    CHECK(d->errorLine == 1);
    const char* bad3 = "template t F0 v F7\nparam t 0 \"A\" 0 1\n";
    CHECK(!parse(bad3, std::strlen(bad3), *d)); // param before a group
    const char* bad4 = "template t F0 v F7\ngroup \"G\"\nparam t 0 \"A\" 0 10 init=11\n";
    CHECK(!parse(bad4, std::strlen(bad4), *d));
    delete d;
}

// The queue

struct Sent
{
    std::vector<uint8_t> bytes;
    uint8_t ports;
};
static std::vector<Sent> sent;
static void record(const uint8_t* data, uint32_t length, uint8_t ports)
{
    sent.push_back({std::vector<uint8_t>(data, data + length), ports});
}

static void testQueue()
{
    sent.clear();
    SysexQueue q;
    const uint8_t a1[] = {0xF0, 1, 0xF7};
    const uint8_t a2[] = {0xF0, 2, 0xF7};
    const uint8_t b[] = {0xF0, 9, 0xF7};
    CHECK(q.push(1, a1, 3, 1, 30));
    CHECK(q.push(2, b, 3, 1, 30));
    CHECK(q.push(1, a2, 3, 1, 30)); // replaces a1, keeps its place
    CHECK(q.waiting() == 2);

    q.pump(0, record);
    CHECK(sent.size() == 1 && sent[0].bytes[1] == 2);
    q.pump(10000, record); // 10 ms: still within the 30 ms pause
    CHECK(sent.size() == 1);
    q.pump(30000, record);
    CHECK(sent.size() == 2 && sent[1].bytes[1] == 9);
    q.pump(90000, record);
    CHECK(sent.size() == 2 && q.waiting() == 0);

    // Full, and too long.
    for (int i = 0; i < SysexQueue::kCapacity; i++)
        q.push(static_cast<uint32_t>(100 + i), b, 3, 1, 0);
    CHECK(!q.push(999, b, 3, 1, 0));
    uint8_t big[SysexQueue::kMaxLength + 1] = {};
    CHECK(!q.push(1000, big, sizeof(big), 1, 0));
}

// A TX81Z-style dump: a request, then replies with a header, data and a
// Yamaha checksum (the same sum as Roland's).
static void testYamahaDump()
{
    auto d = load("synth \"4op\"\n"
                  "template vced F0 43 d+10 12 p v F7\n"
                  "template aced F0 43 d+10 13 p v F7\n"
                  "request F0 43 d+20 7E 4C 4D 20 20 38 39 37 36 41 45 F7\n"
                  "dump vced F0 43 d+00 03 00 5D [ data ] F7\n"
                  "dump aced F0 43 d+00 7E 00 21 [ 4C 4D 20 20 38 39 37 36 41 45 data ] F7\n"
                  "name vced 77 10\n"
                  "group \"G\"\n"
                  "param vced 52 \"Algorithm\" 0 7\n"
                  "param vced 62 \"Transpose\" 0 48 show=-24\n"
                  "param aced 20 \"Reverb\" 0 7\n"
                  "param vced 0 \"Offset\" -5 5 send=5\n");
    CHECK(d->requestCount == 1 && d->dumpCount == 2 && d->nameTempl == 0);

    uint8_t req[kMaxMessage];
    size_t n = buildRequest(*d, 0, 3, req, sizeof(req));
    CHECK(n == 15 && req[2] == 0x22 && req[3] == 0x7E && req[4] == 'L' && req[14] == 0xF7);

    // VCED from device 3: 93 values, the name "BRASS" at 77.
    std::vector<uint8_t> vced = { 0xF0, 0x43, 0x02, 0x03, 0x00, 0x5D };
    std::vector<uint8_t> data(93, 0);
    data[0] = 8; // Offset: 8 - send 5 = 3
    data[52] = 5;
    data[62] = 30;
    const char* name = "BRASS     ";
    for (int i = 0; i < 10; i++)
        data[77 + i] = static_cast<uint8_t>(name[i]);
    vced.insert(vced.end(), data.begin(), data.end());
    vced.push_back(rolandChecksum(data.data(), data.size()));
    vced.push_back(0xF7);

    DumpData dd;
    CHECK(matchDump(*d, vced.data(), vced.size(), 3, dd) && dd.dump == 0 && dd.length == 93);
    int16_t values[4] = { -1, -1, -1, -1 };
    CHECK(applyDump(*d, dd, values) == 3);
    CHECK(values[0] == 5 && values[1] == 30 && values[2] == -1 && values[3] == 3);
    char text[24];
    CHECK(dumpName(*d, dd, text, sizeof(text)) && std::strcmp(text, "BRASS") == 0);

    // Wrong device, or a bad checksum: not ours.
    CHECK(!matchDump(*d, vced.data(), vced.size(), 1, dd));
    vced[vced.size() - 2] ^= 1;
    CHECK(!matchDump(*d, vced.data(), vced.size(), 3, dd));

    // ACED: the checksum covers the header too.
    std::vector<uint8_t> acedData = { 'L', 'M', ' ', ' ', '8', '9', '7', '6', 'A', 'E' };
    for (int i = 0; i < 23; i++)
        acedData.push_back(i == 20 ? 6 : 0);
    std::vector<uint8_t> aced = { 0xF0, 0x43, 0x02, 0x7E, 0x00, 0x21 };
    aced.insert(aced.end(), acedData.begin(), acedData.end());
    aced.push_back(rolandChecksum(acedData.data(), acedData.size()));
    aced.push_back(0xF7);
    CHECK(matchDump(*d, aced.data(), aced.size(), 3, dd) && dd.dump == 1 && dd.length == 23);
    CHECK(applyDump(*d, dd, values) == 1 && values[2] == 6);
    CHECK(!dumpName(*d, dd, text, sizeof(text)));
}

// A Roland DT1 reply: the address says where the data starts.
static void testRolandDump()
{
    auto d = load("synth \"R\"\n"
                  "template dt1 F0 41 d+10 16 12 [ a2 a1 a0 v ] F7\n"
                  "request F0 41 d+10 16 11 [ 04 00 00 00 00 7F ] F7\n"
                  "dump dt1 F0 41 d+10 16 12 [ a2 a1 a0 data ] F7\n"
                  "group \"G\"\n"
                  "param dt1 0x040000 \"First\" 0 127\n"
                  "param dt1 0x040102 \"Over a byte\" 0 127\n"
                  "param dt1 0x050000 \"Elsewhere\" 0 127\n");
    uint8_t req[kMaxMessage];
    size_t n = buildRequest(*d, 0, 1, req, sizeof(req));
    CHECK(n == 13 && req[2] == 0x10 && req[11] == rolandChecksum(req + 5, 6));

    // 200 bytes from 04 00 00: index 130 is 04 01 02 (address bytes are 7 bits).
    std::vector<uint8_t> body = { 0x04, 0x00, 0x00 };
    for (int i = 0; i < 200; i++)
        body.push_back(static_cast<uint8_t>(i % 128));
    std::vector<uint8_t> msg = { 0xF0, 0x41, 0x10, 0x16, 0x12 };
    msg.insert(msg.end(), body.begin(), body.end());
    msg.push_back(rolandChecksum(body.data(), body.size()));
    msg.push_back(0xF7);

    DumpData dd;
    CHECK(matchDump(*d, msg.data(), msg.size(), 1, dd) && dd.length == 200 && dd.address == 0x040000);
    int16_t values[3] = { -1, -1, -1 };
    CHECK(applyDump(*d, dd, values) == 2);
    CHECK(values[0] == 0 && values[1] == 130 % 128 && values[2] == -1);
}

static void testDumpErrors()
{
    auto bad = [](const char* text) {
        Definition d;
        return !parse(text, std::strlen(text), d);
    };
    const char* head = "template t F0 01 p v F7\ngroup \"G\"\nparam t 0 \"A\" 0 1\n";
    char buf[256];
    std::snprintf(buf, sizeof(buf), "%srequest F0 01 v F7\n", head);
    CHECK(bad(buf));
    std::snprintf(buf, sizeof(buf), "%sdump t F0 01 F7\n", head);
    CHECK(bad(buf));
    std::snprintf(buf, sizeof(buf), "%sdump nope F0 data F7\n", head);
    CHECK(bad(buf));
    std::snprintf(buf, sizeof(buf), "%sdump t F0 data data F7\n", head);
    CHECK(bad(buf));
    std::snprintf(buf, sizeof(buf), "%sname t 5\n", head);
    CHECK(bad(buf));
    std::snprintf(buf, sizeof(buf), "%sdump t F0 01 data F7\nname t 0 4\n", head);
    CHECK(!bad(buf));
}

int main()
{
    testYamaha();
    testRolandChecksum();
    testEmu14Bit();
    testErrors();
    testQueue();
    testYamahaDump();
    testRolandDump();
    testDumpErrors();
    if (failures == 0)
        std::printf("all synth definition tests passed\n");
    return failures == 0 ? 0 : 1;
}
