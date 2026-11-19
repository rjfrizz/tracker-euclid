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
    else if (strcmp(w, "[") == 0)
        t.type = Token::sumStart;
    else if (strcmp(w, "]") == 0)
        t.type = Token::checksum;
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
} // namespace

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
            t.count = 0;
            int depth = 0;
            while (line.word(w, sizeof(w)))
            {
                if (t.count >= kMaxTokens)
                    return fail(d, lineNumber, "template too long");
                if (!parseToken(w, t.tokens[t.count]))
                    return fail(d, lineNumber, "template: bad word");
                if (t.tokens[t.count].type == Token::sumStart)
                    depth++;
                if (t.tokens[t.count].type == Token::checksum && --depth < 0)
                    return fail(d, lineNumber, "template: ] without [");
                t.count++;
            }
            if (t.count == 0 || depth != 0)
                return fail(d, lineNumber, "template: empty or [ unclosed");
            d.templateCount++;
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

size_t buildMessage(const Definition& d, uint16_t param, int value, uint8_t device, uint8_t* out, size_t maxLength)
{
    if (param >= d.paramCount)
        return 0;
    const Param& p = d.params[param];
    const Template& t = d.templates[p.templ];
    if (value < p.min)
        value = p.min;
    if (value > p.max)
        value = p.max;
    const unsigned v = static_cast<unsigned>(value + p.send) & 0x3FFF;
    const uint32_t n = p.number;

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
            case Token::address2: b = static_cast<uint8_t>((n >> 16) & 0x7F); break;
            case Token::address1: b = static_cast<uint8_t>((n >> 8) & 0x7F); break;
            case Token::address0: b = static_cast<uint8_t>(n & 0x7F); break;
            case Token::value7: b = static_cast<uint8_t>(v & 0x7F); break;
            case Token::valueHigh: b = static_cast<uint8_t>((v >> 7) & 0x7F); break;
            case Token::valueLow: b = static_cast<uint8_t>(v & 0x7F); break;
            case Token::sumStart: sumFrom = len; continue;
            case Token::checksum: b = rolandChecksum(out + sumFrom, len - sumFrom); break;
        }
        if (len >= maxLength)
            return 0;
        out[len++] = b;
    }
    return len;
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
