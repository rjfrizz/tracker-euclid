#include "synthDef.h"

#include <stdio.h>
#include <string.h>

namespace euclid
{
namespace synth
{

namespace
{
// A cursor over one line.
struct Line
{
    const char* p;
    const char* end;

    void skipSpace()
    {
        while (p < end && (*p == ' ' || *p == '\t'))
            p++;
    }

    bool atEnd()
    {
        skipSpace();
        return p >= end || *p == '#';
    }

    // The next word or "quoted string", without the quotes.
    bool word(char* out, size_t length)
    {
        skipSpace();
        if (p >= end || *p == '#')
            return false;
        size_t n = 0;
        if (*p == '"')
        {
            p++;
            while (p < end && *p != '"')
            {
                if (n + 1 < length)
                    out[n++] = *p;
                p++;
            }
            if (p < end)
                p++; // closing quote
        }
        else
        {
            while (p < end && *p != ' ' && *p != '\t' && *p != '#')
            {
                if (n + 1 < length)
                    out[n++] = *p;
                p++;
            }
        }
        out[n] = 0;
        return true;
    }
};

bool parseInt(const char* s, long& out)
{
    if (*s == 0)
        return false;
    bool neg = false;
    if (*s == '-' || *s == '+')
    {
        neg = *s == '-';
        s++;
    }
    long v = 0;
    int base = 10;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
    {
        base = 16;
        s += 2;
    }
    if (*s == 0)
        return false;
    for (; *s; s++)
    {
        int digit;
        if (*s >= '0' && *s <= '9')
            digit = *s - '0';
        else if (base == 16 && *s >= 'a' && *s <= 'f')
            digit = *s - 'a' + 10;
        else if (base == 16 && *s >= 'A' && *s <= 'F')
            digit = *s - 'A' + 10;
        else
            return false;
        v = v * base + digit;
    }
    out = neg ? -v : v;
    return true;
}

bool parseHexByte(const char* s, uint8_t& out)
{
    if (strlen(s) != 2)
        return false;
    long v;
    char buf[8];
    snprintf(buf, sizeof(buf), "0x%s", s);
    if (!parseInt(buf, v) || v < 0 || v > 255)
        return false;
    out = static_cast<uint8_t>(v);
    return true;
}

bool fail(Definition& d, uint16_t line, const char* message)
{
    d.errorLine = line;
    snprintf(d.error, sizeof(d.error), "%s", message);
    return false;
}

bool addString(Definition& d, const char* s, uint16_t& offset)
{
    size_t n = strlen(s) + 1;
    if (d.poolUsed + n > static_cast<size_t>(kStringPool))
        return false;
    offset = d.poolUsed;
    memcpy(d.pool + d.poolUsed, s, n);
    d.poolUsed = static_cast<uint16_t>(d.poolUsed + n);
    return true;
}

int findTemplate(const Definition& d, const char* name)
{
    for (int i = 0; i < d.templateCount; i++)
        if (strcmp(d.templates[i].name, name) == 0)
            return i;
    return -1;
}

int findList(const Definition& d, const char* name)
{
    for (int i = 0; i < d.listCount; i++)
        if (strcmp(d.listNames[i], name) == 0)
            return i;
    return -1;
}

bool parseToken(const char* w, TemplateToken& t)
{
    t.value = 0;
    if (strcmp(w, "p") == 0)
        t.type = Token::paramLow7;
    else if (strcmp(w, "ph") == 0)
        t.type = Token::paramHigh;
    else if (strcmp(w, "pl") == 0)
        t.type = Token::paramLow;
    else if (strcmp(w, "a3") == 0)
        t.type = Token::address3;
    else if (strcmp(w, "a2") == 0)
        t.type = Token::address2;
    else if (strcmp(w, "a1") == 0)
        t.type = Token::address1;
    else if (strcmp(w, "a0") == 0)
        t.type = Token::address0;
    else if (strcmp(w, "v") == 0)
        t.type = Token::value7;
    else if (strcmp(w, "vh") == 0)
        t.type = Token::valueHigh;
    else if (strcmp(w, "vl") == 0)
        t.type = Token::valueLow;
    else if (strcmp(w, "n1") == 0)
        t.type = Token::nibbleHigh;
    else if (strcmp(w, "n0") == 0)
        t.type = Token::nibbleLow;
    else if (strcmp(w, "[") == 0)
        t.type = Token::sumStart;
    else if (strcmp(w, "]") == 0)
        t.type = Token::checksum;
    else if (strcmp(w, "data") == 0)
        t.type = Token::data;
    else if (strcmp(w, "|") == 0)
        t.type = Token::split;
    else if (w[0] == 'd' && w[1] == '+')
    {
        t.type = Token::device;
        return parseHexByte(w + 2, t.value);
    }
    else
    {
        t.type = Token::literal;
        return parseHexByte(w, t.value);
    }
    return true;
}

enum Use : uint8_t
{
    useTemplate, // a param's message
    useRequest,
    useDump,
};

// The rest of the line as message words. Null if fine, or what's wrong.
const char* readTokens(Line& line, Template& t, Use use)
{
    char w[64];
    t.count = 0;
    int depth = 0;
    int data = 0;
    while (line.word(w, sizeof(w)))
    {
        if (t.count >= kMaxTokens)
            return "message too long";
        TemplateToken& tok = t.tokens[t.count];
        if (!parseToken(w, tok))
            return "bad word in message";
        switch (tok.type)
        {
            case Token::sumStart: depth++; break;
            case Token::checksum:
                if (--depth < 0)
                    return "] without [";
                break;
            case Token::data:
                if (use != useDump)
                    return "data only goes in a dump";
                data++;
                break;
            case Token::split:
                if (use != useTemplate || depth != 0)
                    return "| only between a template's messages";
                break;
            case Token::literal:
            case Token::device: break;
            case Token::address3:
            case Token::address2:
            case Token::address1:
            case Token::address0: break;
            default:
                if (use != useTemplate)
                    return "no p or v in a request or dump";
                break;
        }
        t.count++;
    }
    if (t.count == 0 || depth != 0)
        return "message empty or [ unclosed";
    if (use == useDump && data != 1)
        return "a dump needs one data";
    return nullptr;
}

// Roland addresses are 7 bits a byte; this makes them plain numbers.
uint32_t linearAddress(uint32_t a)
{
    return ((a >> 24) & 0x7F) * 2097152u + ((a >> 16) & 0x7F) * 16384u + ((a >> 8) & 0x7F) * 128u + (a & 0x7F);
}

uint32_t printedAddress(uint32_t linear)
{
    return ((linear / 2097152u) & 0x7F) << 24 | ((linear / 16384u) & 0x7F) << 16 | ((linear / 128u) & 0x7F) << 8 |
           (linear & 0x7F);
}

bool hasAddress(const Template& t)
{
    for (uint8_t i = 0; i < t.count; i++)
        if (t.tokens[i].type >= Token::address3 && t.tokens[i].type <= Token::address0)
            return true;
    return false;
}

// A number or address moved to a part.
uint32_t forPart(const Definition& d, uint32_t n, bool address, uint8_t part, uint16_t stride)
{
    if (d.partCount == 0 || part <= 1 || part > d.partCount)
        return n;
    const uint32_t offset = static_cast<uint32_t>(part - 1) * (stride != 0 ? stride : d.partStride);
    return address ? printedAddress(linearAddress(n) + offset) : n + offset;
}

size_t emit(const Template& t, uint32_t n, unsigned v, uint8_t device, uint8_t* out, size_t maxLength)
{
    size_t len = 0;
    size_t sumFrom = 0;
    for (uint8_t i = 0; i < t.count; i++)
    {
        const TemplateToken& tok = t.tokens[i];
        uint8_t b = 0;
        switch (tok.type)
        {
            case Token::literal: b = tok.value; break;
            case Token::device: b = static_cast<uint8_t>(tok.value + device - 1); break;
            case Token::paramLow7: b = static_cast<uint8_t>(n & 0x7F); break;
            case Token::paramHigh: b = static_cast<uint8_t>((n >> 7) & 0x7F); break;
            case Token::paramLow: b = static_cast<uint8_t>(n & 0x7F); break;
            case Token::address3: b = static_cast<uint8_t>((n >> 24) & 0x7F); break;
            case Token::address2: b = static_cast<uint8_t>((n >> 16) & 0x7F); break;
            case Token::nibbleHigh: b = static_cast<uint8_t>((v >> 4) & 0x0F); break;
            case Token::nibbleLow: b = static_cast<uint8_t>(v & 0x0F); break;
            case Token::address1: b = static_cast<uint8_t>((n >> 8) & 0x7F); break;
            case Token::address0: b = static_cast<uint8_t>(n & 0x7F); break;
            case Token::value7: b = static_cast<uint8_t>(v & 0x7F); break;
            case Token::valueHigh: b = static_cast<uint8_t>((v >> 7) & 0x7F); break;
            case Token::valueLow: b = static_cast<uint8_t>(v & 0x7F); break;
            case Token::sumStart: sumFrom = len; continue;
            case Token::checksum: b = rolandChecksum(out + sumFrom, len - sumFrom); break;
            case Token::data: return 0; // never sent
            case Token::split: sumFrom = len; continue;
        }
        if (len >= maxLength)
            return 0;
        out[len++] = b;
    }
    return len;
}
} // namespace

size_t messageLength(const uint8_t* b, size_t count)
{
    if (count == 0)
        return 0;
    if (b[0] == 0xF0)
    {
        for (size_t i = 1; i < count; i++)
            if (b[i] == 0xF7)
                return i + 1;
        return 0;
    }
    const uint8_t type = b[0] & 0xF0;
    size_t n = type == 0xC0 || type == 0xD0 ? 2 : (type >= 0x80 && type < 0xF0 ? 3 : 0);
    return n <= count ? n : 0;
}

uint8_t rolandChecksum(const uint8_t* bytes, size_t count)
{
    unsigned sum = 0;
    for (size_t i = 0; i < count; i++)
        sum += bytes[i];
    return static_cast<uint8_t>((128 - (sum % 128)) % 128);
}

bool parse(const char* text, size_t length, Definition& d)
{
    memset(&d, 0, sizeof(d));
    snprintf(d.name, sizeof(d.name), "Synth");
    d.pauseMs = 20;
    d.device = 1;
    d.nameTempl = 0xFF;

    uint8_t group = 0xFF;
    uint16_t lineNumber = 0;
    const char* p = text;
    const char* end = text + length;

    while (p < end)
    {
        const char* lineEnd = p;
        while (lineEnd < end && *lineEnd != '\n')
            lineEnd++;
        lineNumber++;
        Line line{ p, lineEnd > p && lineEnd[-1] == '\r' ? lineEnd - 1 : lineEnd };
        p = lineEnd < end ? lineEnd + 1 : end;

        char key[16];
        if (line.atEnd() || !line.word(key, sizeof(key)))
            continue;

        char w[64];
        if (strcmp(key, "synth") == 0)
        {
            if (!line.word(d.name, sizeof(d.name)))
                return fail(d, lineNumber, "synth: name missing");
        }
        else if (strcmp(key, "pause") == 0 || strcmp(key, "device") == 0)
        {
            long v;
            if (!line.word(w, sizeof(w)) || !parseInt(w, v) || v < 0 || v > 2000)
                return fail(d, lineNumber, "number expected");
            if (key[0] == 'p')
                d.pauseMs = static_cast<uint16_t>(v);
            else
            {
                if (v < 1 || v > 16)
                    return fail(d, lineNumber, "device: 1-16");
                d.device = static_cast<uint8_t>(v);
            }
        }
        else if (strcmp(key, "port") == 0)
        {
            if (!line.word(w, sizeof(w)))
                return fail(d, lineNumber, "port: din, usb, both or notes");
            if (strcmp(w, "din") == 0)
                d.ports = 1;
            else if (strcmp(w, "usb") == 0)
                d.ports = 2;
            else if (strcmp(w, "both") == 0)
                d.ports = 3;
            else if (strcmp(w, "notes") == 0)
                d.ports = 0;
            else
                return fail(d, lineNumber, "port: din, usb, both or notes");
        }
        else if (strcmp(key, "template") == 0)
        {
            if (d.templateCount >= kMaxTemplates)
                return fail(d, lineNumber, "too many templates");
            Template& t = d.templates[d.templateCount];
            if (!line.word(t.name, sizeof(t.name)))
                return fail(d, lineNumber, "template: name missing");
            if (const char* e = readTokens(line, t, useTemplate))
                return fail(d, lineNumber, e);
            d.templateCount++;
        }
        else if (strcmp(key, "request") == 0)
        {
            if (d.requestCount >= kMaxRequests)
                return fail(d, lineNumber, "too many requests");
            Template& t = d.requests[d.requestCount];
            d.requestAddress[d.requestCount] = 0;
            d.requestStride[d.requestCount] = 0;
            for (;;)
            {
                const char* save = line.p;
                long a;
                const bool got = line.word(w, sizeof(w));
                if (got && strncmp(w, "addr=", 5) == 0)
                {
                    if (!parseInt(w + 5, a) || a < 0)
                        return fail(d, lineNumber, "request: bad addr=");
                    d.requestAddress[d.requestCount] = static_cast<uint32_t>(a);
                }
                else if (got && strncmp(w, "stride=", 7) == 0)
                {
                    if (!parseInt(w + 7, a) || a < 1 || a > 65535)
                        return fail(d, lineNumber, "request: bad stride=");
                    d.requestStride[d.requestCount] = static_cast<uint16_t>(a);
                }
                else
                {
                    line.p = save;
                    break;
                }
            }
            if (const char* e = readTokens(line, t, useRequest))
                return fail(d, lineNumber, e);
            d.requestCount++;
        }
        else if (strcmp(key, "dump") == 0)
        {
            if (d.dumpCount >= kMaxDumps)
                return fail(d, lineNumber, "too many dumps");
            Dump& dump = d.dumps[d.dumpCount];
            // One template, or several joined by +.
            if (!line.word(w, sizeof(w)))
                return fail(d, lineNumber, "dump: unknown template");
            dump.templMask = 0;
            bool first = true;
            for (char* name = w; name != nullptr && *name != 0;)
            {
                char* plus = strchr(name, '+');
                if (plus != nullptr)
                    *plus = 0;
                int t = findTemplate(d, name);
                if (t < 0)
                    return fail(d, lineNumber, "dump: unknown template");
                if (first)
                    dump.templ = static_cast<uint8_t>(t);
                first = false;
                dump.templMask = static_cast<uint16_t>(dump.templMask | (1u << t));
                name = plus != nullptr ? plus + 1 : nullptr;
            }
            if (const char* e = readTokens(line, dump.pattern, useDump))
                return fail(d, lineNumber, e);
            dump.hasAddress = hasAddress(dump.pattern);
            d.dumpCount++;
        }
        else if (strcmp(key, "map") == 0)
        {
            if (d.mapCount >= kMaxMaps)
                return fail(d, lineNumber, "too many maps");
            const uint8_t m = d.mapCount;
            if (!line.word(d.mapNames[m], sizeof(d.mapNames[0])))
                return fail(d, lineNumber, "map: name missing");
            d.mapFirst[m] = d.mapUsed;
            d.mapLength[m] = 0;
            long v;
            while (line.word(w, sizeof(w)))
            {
                if (!parseInt(w, v) || v < 0 || v > 0x3FFF || d.mapUsed >= kMapPool)
                    return fail(d, lineNumber, "map: numbers 0-16383, not too many");
                d.mapPool[d.mapUsed++] = static_cast<uint16_t>(v);
                d.mapLength[m]++;
            }
            d.mapCount++;
        }
        else if (strcmp(key, "listen") == 0)
        {
            d.listen = true;
        }
        else if (strcmp(key, "parts") == 0)
        {
            long count, stride;
            if (!line.word(w, sizeof(w)) || !parseInt(w, count) || count < 1 || count > 16 ||
                !line.word(w, sizeof(w)) || !parseInt(w, stride) || stride < 1 || stride > 65535)
                return fail(d, lineNumber, "parts: count (1-16) and distance");
            d.partCount = static_cast<uint8_t>(count);
            d.partStride = static_cast<uint16_t>(stride);
            if (line.word(w, sizeof(w)))
            {
                long ch;
                if (strncmp(w, "channel=", 8) != 0 || !parseInt(w + 8, ch) || ch < 1 || ch > 16)
                    return fail(d, lineNumber, "parts: channel=1-16 expected");
                d.partChannel = static_cast<uint8_t>(ch);
            }
        }
        else if (strcmp(key, "name") == 0)
        {
            long number, len;
            int t = line.word(w, sizeof(w)) ? findTemplate(d, w) : -1;
            if (t < 0)
                return fail(d, lineNumber, "name: unknown template");
            if (!line.word(w, sizeof(w)) || !parseInt(w, number) || number < 0 || !line.word(w, sizeof(w)) ||
                !parseInt(w, len) || len < 1 || len > 24)
                return fail(d, lineNumber, "name: number and length expected");
            d.nameTempl = static_cast<uint8_t>(t);
            d.nameNumber = static_cast<uint32_t>(number);
            d.nameLength = static_cast<uint8_t>(len);
        }
        else if (strcmp(key, "list") == 0)
        {
            if (d.listCount >= kMaxLists)
                return fail(d, lineNumber, "too many lists");
            List& l = d.lists[d.listCount];
            if (!line.word(d.listNames[d.listCount], sizeof(d.listNames[0])))
                return fail(d, lineNumber, "list: name missing");
            l.count = 0;
            l.first = d.poolUsed;
            while (line.word(w, sizeof(w)))
            {
                uint16_t offset;
                if (!addString(d, w, offset))
                    return fail(d, lineNumber, "out of string space");
                l.count++;
            }
            d.listCount++;
        }
        else if (strcmp(key, "group") == 0)
        {
            if (d.groupCount >= kMaxGroups)
                return fail(d, lineNumber, "too many groups");
            Group& g = d.groups[d.groupCount];
            if (!line.word(w, sizeof(w)) || !addString(d, w, g.name))
                return fail(d, lineNumber, "group: name missing");
            g.first = d.paramCount;
            g.count = 0;
            group = d.groupCount++;
        }
        else if (strcmp(key, "param") == 0)
        {
            if (group == 0xFF)
                return fail(d, lineNumber, "param before any group");
            if (d.paramCount >= kMaxParams)
                return fail(d, lineNumber, "too many params");
            Param& prm = d.params[d.paramCount];
            prm.group = group;
            prm.list = 0xFF;
            prm.show = 0;
            prm.send = 0;
            prm.stride = 0;
            prm.reload = false;
            prm.map = 0xFF;
            prm.nibbles = 0;

            if (!line.word(w, sizeof(w)))
                return fail(d, lineNumber, "param: template missing");
            int t = findTemplate(d, w);
            if (t < 0)
                return fail(d, lineNumber, "param: unknown template");
            prm.templ = static_cast<uint8_t>(t);

            long number, lo, hi;
            if (!line.word(w, sizeof(w)) || !parseInt(w, number) || number < 0)
                return fail(d, lineNumber, "param: number expected");
            prm.number = static_cast<uint32_t>(number);
            if (!line.word(w, sizeof(w)) || !addString(d, w, prm.name))
                return fail(d, lineNumber, "param: name missing");
            if (!line.word(w, sizeof(w)) || !parseInt(w, lo) || !line.word(w, sizeof(w)) || !parseInt(w, hi) ||
                lo > hi || lo < -16384 || hi > 16383)
                return fail(d, lineNumber, "param: min and max expected");
            prm.min = static_cast<int16_t>(lo);
            prm.max = static_cast<int16_t>(hi);
            prm.init = prm.min;

            while (line.word(w, sizeof(w)))
            {
                char* eq = strchr(w, '=');
                if (eq == nullptr)
                    return fail(d, lineNumber, "param: option=value expected");
                *eq = 0;
                const char* value = eq + 1;
                long v = 0;
                if (strcmp(w, "map") == 0)
                {
                    int m = -1;
                    for (int i = 0; i < d.mapCount; i++)
                        if (strcmp(d.mapNames[i], value) == 0)
                            m = i;
                    if (m < 0)
                        return fail(d, lineNumber, "param: unknown map");
                    prm.map = static_cast<uint8_t>(m);
                    continue;
                }
                if (strcmp(w, "list") == 0)
                {
                    int l = findList(d, value);
                    if (l < 0)
                        return fail(d, lineNumber, "param: unknown list");
                    prm.list = static_cast<uint8_t>(l);
                    continue;
                }
                if (!parseInt(value, v))
                    return fail(d, lineNumber, "param: bad option value");
                if (strcmp(w, "show") == 0)
                    prm.show = static_cast<int16_t>(v);
                else if (strcmp(w, "send") == 0)
                    prm.send = static_cast<int16_t>(v);
                else if (strcmp(w, "init") == 0)
                    prm.init = static_cast<int16_t>(v);
                else if (strcmp(w, "stride") == 0 && v >= 1 && v <= 65535)
                    prm.stride = static_cast<uint16_t>(v);
                else if (strcmp(w, "reload") == 0)
                    prm.reload = v != 0;
                else if (strcmp(w, "nibbles") == 0 && (v == 0 || v == 2))
                    prm.nibbles = static_cast<uint8_t>(v);
                else
                    return fail(d, lineNumber, "param: unknown option");
            }
            if (prm.init < prm.min || prm.init > prm.max)
                return fail(d, lineNumber, "param: init out of range");
            d.groups[group].count++;
            d.paramCount++;
        }
        else
        {
            return fail(d, lineNumber, "unknown line");
        }
    }

    if (d.paramCount == 0)
        return fail(d, lineNumber, "no params");
    return true;
}

size_t buildMessage(const Definition& d, uint16_t param, int value, uint8_t device, uint8_t part, uint8_t* out,
                    size_t maxLength)
{
    if (param >= d.paramCount)
        return 0;
    const Param& p = d.params[param];
    const Template& t = d.templates[p.templ];
    if (value < p.min)
        value = p.min;
    if (value > p.max)
        value = p.max;
    unsigned v = static_cast<unsigned>(value + p.send) & 0x3FFF;
    if (p.map != 0xFF)
    {
        const int i = value - p.min;
        v = i >= 0 && i < d.mapLength[p.map] ? d.mapPool[d.mapFirst[p.map] + i] : 0;
    }
    return emit(t, forPart(d, p.number, hasAddress(t), part, p.stride), v, device, out, maxLength);
}

size_t buildRequest(const Definition& d, uint8_t request, uint8_t device, uint8_t part, uint8_t* out,
                    size_t maxLength)
{
    if (request >= d.requestCount)
        return 0;
    const Template& t = d.requests[request];
    return emit(t, forPart(d, d.requestAddress[request], hasAddress(t), part, d.requestStride[request]), 0, device,
                out, maxLength);
}

bool matchDump(const Definition& d, const uint8_t* msg, size_t length, uint8_t device, DumpData& out)
{
    for (uint8_t k = 0; k < d.dumpCount; k++)
    {
        const Template& t = d.dumps[k].pattern;
        size_t fixed = 0; // bytes besides the data
        for (uint8_t i = 0; i < t.count; i++)
            if (t.tokens[i].type != Token::sumStart && t.tokens[i].type != Token::data)
                fixed++;
        if (length <= fixed)
            continue;
        const size_t dataLength = length - fixed;

        size_t pos = 0;
        size_t sumFrom = 0;
        uint32_t address = 0;
        const uint8_t* data = nullptr;
        bool ok = true;
        for (uint8_t i = 0; i < t.count && ok; i++)
        {
            const TemplateToken& tok = t.tokens[i];
            switch (tok.type)
            {
                case Token::literal: ok = msg[pos++] == tok.value; break;
                case Token::device: ok = msg[pos++] == static_cast<uint8_t>(tok.value + device - 1); break;
                case Token::address3: address |= static_cast<uint32_t>(msg[pos++]) << 24; break;
                case Token::address2: address |= static_cast<uint32_t>(msg[pos++]) << 16; break;
                case Token::address1: address |= static_cast<uint32_t>(msg[pos++]) << 8; break;
                case Token::address0: address |= msg[pos++]; break;
                case Token::sumStart: sumFrom = pos; break;
                case Token::checksum:
                    ok = rolandChecksum(msg + sumFrom, pos - sumFrom) == msg[pos];
                    pos++;
                    break;
                case Token::data:
                    data = msg + pos;
                    pos += dataLength;
                    break;
                default: ok = false; break;
            }
        }
        if (ok && data != nullptr)
        {
            out.dump = k;
            out.data = data;
            out.length = dataLength;
            out.address = address;
            return true;
        }
    }
    return false;
}

namespace
{
// Where a template's number falls in a dump's data, or -1.
long dumpIndex(const Definition& d, const DumpData& dump, uint32_t number, uint8_t part, uint16_t stride)
{
    const bool address = d.dumps[dump.dump].hasAddress;
    number = forPart(d, number, address, part, stride);
    long i = address ? static_cast<long>(linearAddress(number)) - static_cast<long>(linearAddress(dump.address))
                     : static_cast<long>(number);
    return i >= 0 && static_cast<size_t>(i) < dump.length ? i : -1;
}
} // namespace

int applyDump(const Definition& d, const DumpData& dump, uint8_t part, int16_t* values)
{
    if (dump.dump >= d.dumpCount)
        return 0;
    const uint16_t mask = d.dumps[dump.dump].templMask;
    int count = 0;
    for (uint16_t i = 0; i < d.paramCount; i++)
    {
        const Param& p = d.params[i];
        if (!((mask >> p.templ) & 1u))
            continue;
        long at = dumpIndex(d, dump, p.number, part, p.stride);
        if (at < 0 || (p.nibbles == 2 && static_cast<size_t>(at + 1) >= dump.length))
            continue;
        const int raw = p.nibbles == 2 ? ((dump.data[at] & 0x0F) << 4) | (dump.data[at + 1] & 0x0F) : dump.data[at];
        int v = raw - p.send;
        if (p.map != 0xFF)
        {
            // Back from the number to the value that sends it.
            v = p.min;
            for (int k = 0; k < d.mapLength[p.map]; k++)
                if (d.mapPool[d.mapFirst[p.map] + k] == raw)
                    v = p.min + k;
        }
        values[i] = static_cast<int16_t>(v < p.min ? p.min : (v > p.max ? p.max : v));
        count++;
    }
    return count;
}

namespace
{
// Writes a dump's device, address and checksums into a message of its
// pattern (the data is already in place).
void rewrite(const Template& t, uint8_t* msg, size_t length, uint8_t device, uint32_t address)
{
    size_t fixed = 0;
    for (uint8_t i = 0; i < t.count; i++)
        if (t.tokens[i].type != Token::sumStart && t.tokens[i].type != Token::data)
            fixed++;
    const size_t dataLength = length - fixed;
    size_t pos = 0;
    size_t sumFrom = 0;
    for (uint8_t i = 0; i < t.count; i++)
    {
        const TemplateToken& tok = t.tokens[i];
        switch (tok.type)
        {
            case Token::device: msg[pos++] = static_cast<uint8_t>(tok.value + device - 1); break;
            case Token::address3: msg[pos++] = static_cast<uint8_t>((address >> 24) & 0x7F); break;
            case Token::address2: msg[pos++] = static_cast<uint8_t>((address >> 16) & 0x7F); break;
            case Token::address1: msg[pos++] = static_cast<uint8_t>((address >> 8) & 0x7F); break;
            case Token::address0: msg[pos++] = static_cast<uint8_t>(address & 0x7F); break;
            case Token::sumStart: sumFrom = pos; break;
            case Token::checksum:
                msg[pos] = rolandChecksum(msg + sumFrom, pos - sumFrom);
                pos++;
                break;
            case Token::data: pos += dataLength; break;
            default: pos++; break;
        }
    }
}
} // namespace

bool fillDump(const Definition& d, const int16_t* values, uint8_t part, uint8_t device, uint8_t* msg, size_t length)
{
    DumpData dump;
    if (!matchDump(d, msg, length, device, dump))
        return false;
    uint8_t* data = msg + (dump.data - msg);
    const uint16_t mask = d.dumps[dump.dump].templMask;
    for (uint16_t i = 0; i < d.paramCount; i++)
    {
        const Param& p = d.params[i];
        if (!((mask >> p.templ) & 1u))
            continue;
        long at = dumpIndex(d, dump, p.number, part, p.stride);
        if (at < 0 || (p.nibbles == 2 && static_cast<size_t>(at + 1) >= dump.length))
            continue;
        int v = values[i] < p.min ? p.min : (values[i] > p.max ? p.max : values[i]);
        int raw = v + p.send;
        if (p.map != 0xFF)
            raw = v - p.min < d.mapLength[p.map] ? d.mapPool[d.mapFirst[p.map] + v - p.min] : 0;
        if (p.nibbles == 2)
        {
            data[at] = static_cast<uint8_t>((raw >> 4) & 0x0F);
            data[at + 1] = static_cast<uint8_t>(raw & 0x0F);
        }
        else
            data[at] = static_cast<uint8_t>(raw & 0x7F);
    }
    rewrite(d.dumps[dump.dump].pattern, msg, length, device, dump.address);
    return true;
}

bool moveDump(const Definition& d, uint8_t* msg, size_t length, uint8_t fromDevice, uint8_t fromPart,
              uint8_t toDevice, uint8_t toPart)
{
    DumpData dump;
    if (!matchDump(d, msg, length, fromDevice, dump))
        return false;
    uint32_t address = dump.address;
    const Dump& pattern = d.dumps[dump.dump];
    if (pattern.hasAddress && d.partCount > 0 && fromPart != toPart)
    {
        // The distance between parts of the params this dump holds.
        uint16_t stride = d.partStride;
        for (uint16_t i = 0; i < d.paramCount; i++)
            if (((pattern.templMask >> d.params[i].templ) & 1u) &&
                dumpIndex(d, dump, d.params[i].number, fromPart, d.params[i].stride) >= 0)
            {
                stride = d.params[i].stride != 0 ? d.params[i].stride : d.partStride;
                break;
            }
        long linear = static_cast<long>(linearAddress(address)) +
                      (static_cast<long>(toPart) - static_cast<long>(fromPart)) * stride;
        if (linear < 0)
            return false;
        address = printedAddress(static_cast<uint32_t>(linear));
    }
    rewrite(pattern.pattern, msg, length, toDevice, address);
    return true;
}

bool dumpHasSelection(const Definition& d, const DumpData& dump, uint8_t part)
{
    if (dump.dump >= d.dumpCount)
        return false;
    for (uint16_t i = 0; i < d.paramCount; i++)
    {
        const Param& p = d.params[i];
        if (p.reload && ((d.dumps[dump.dump].templMask >> p.templ) & 1u) &&
            dumpIndex(d, dump, p.number, part, p.stride) >= 0)
            return true;
    }
    return false;
}

bool dumpName(const Definition& d, const DumpData& dump, uint8_t part, char* out, size_t length)
{
    if (length == 0 || dump.dump >= d.dumpCount || d.dumps[dump.dump].templ != d.nameTempl)
        return false;
    long first = dumpIndex(d, dump, d.nameNumber, part, 0);
    long last = first < 0 ? -1 : first + d.nameLength - 1;
    if (last >= static_cast<long>(dump.length))
        last = -1;
    if (first < 0 || last < 0)
        return false;
    size_t n = 0;
    for (long i = first; i <= last && n + 1 < length; i++)
    {
        char c = static_cast<char>(dump.data[i]);
        out[n++] = c >= 32 && c < 127 ? c : ' ';
    }
    while (n > 0 && out[n - 1] == ' ')
        n--;
    out[n] = 0;
    return true;
}

void formatValue(const Definition& d, uint16_t param, int value, char* out, size_t length)
{
    if (param >= d.paramCount)
    {
        snprintf(out, length, "-");
        return;
    }
    const Param& p = d.params[param];
    if (p.list != 0xFF)
    {
        const List& l = d.lists[p.list];
        int index = value - p.min;
        if (index >= 0 && index < l.count)
        {
            const char* s = d.pool + l.first;
            for (int i = 0; i < index; i++)
                s += strlen(s) + 1;
            snprintf(out, length, "%s", s);
            return;
        }
    }
    snprintf(out, length, "%d", value + p.show);
}

} // namespace synth
} // namespace euclid
