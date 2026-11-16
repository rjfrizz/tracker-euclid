#include "FT812.h"

#include "display.h"
#include "euclidRingControl.h"

#include <math.h>

namespace
{
constexpr float kTwoPi = 6.28318530718f;

bool isPulseAt(uint8_t steps, uint8_t pulses, uint8_t rotation, uint8_t index)
{
    if (steps == 0 || pulses == 0)
        return false;
    if (pulses >= steps)
        return true;
    int i = (index + steps - (rotation % steps)) % steps;
    return ((i * pulses) % steps) < pulses;
}

void stepPosition(int16_t cx, int16_t cy, int16_t r, uint8_t steps, uint8_t i, int16_t& x,
                  int16_t& y)
{
    // Step 0 at 12 o'clock, running clockwise.
    float a = kTwoPi * static_cast<float>(i) / static_cast<float>(steps) - kTwoPi / 4.0f;
    x = static_cast<int16_t>(cx + r * cosf(a) + 0.5f);
    y = static_cast<int16_t>(cy + r * sinf(a) + 0.5f);
}
} // namespace

cEuclidRing::cEuclidRing(strControlProperties* properties)
  : cDisplayControl(controlIdEuclidRing, 1)
{
    colorsCount = 4;
    colors = nullptr;

    if (properties == nullptr)
    {
        posX = 0;
        posY = 0;
        width = 0;
        height = 0;
        style = 0;
        return;
    }

    colors = properties->colors;
    posX = properties->x;
    posY = properties->y;
    width = properties->w;
    height = properties->h;
    data = static_cast<strEuclidRingData*>(properties->data);

    setStyle(properties->style);
}

cEuclidRing::~cEuclidRing()
{
}

void cEuclidRing::setStyle(uint32_t style)
{
    this->style = style;

    int8_t textFont = FONT_INDEX_FROM_STYLE;
    textFont = (textFont >= 0) ? textFont : 0;
    fontHandle = fonts[textFont].handle;
    // The next font down for the secondary line, when there is one.
    smallFontHandle = fonts[textFont > 0 ? textFont - 1 : 0].handle;
}

void cEuclidRing::setText(const char* text)
{
    this->text = text;
}

void cEuclidRing::setValue(int value)
{
    this->value = value;
}

void cEuclidRing::setColors(const uint32_t* colors)
{
    this->colors = colors;
}

void cEuclidRing::setData(void* data)
{
    this->data = static_cast<strEuclidRingData*>(data);
}

void cEuclidRing::drawRing(int16_t cx, int16_t cy, int16_t r, uint8_t steps, uint8_t pulses,
                           uint8_t rotation, int8_t playhead, bool dim)
{
    if (steps == 0)
        return;

    const Theme& theme = display.getTheme();
    const uint32_t pulseColor = dim ? theme.inactiveLabelsColors[0] : theme.frameColors[0];
    const uint32_t restColor = theme.inactiveLabelsColors[0];
    const uint32_t lineColor = dim ? theme.inactiveLabelsColors[1] : theme.activeLabelsColors[0];

    // Dot size shrinks as the ring fills up.
    uint16_t dot = steps <= 8 ? 14 : (steps <= 16 ? 11 : 7);
    if (dim)
        dot = dot * 2 / 3;

    int16_t x, y;

    // Polygon through the pulses.
    if (pulses >= 2)
    {
        API_COLOR(lineColor);
        API_LINE_WIDTH(dim ? 16 : 24);
        API_BEGIN(LINE_STRIP);
        int16_t firstX = 0, firstY = 0;
        bool first = true;
        for (uint8_t i = 0; i < steps; i++)
        {
            if (!isPulseAt(steps, pulses, rotation, i))
                continue;
            stepPosition(cx, cy, r, steps, i, x, y);
            if (first)
            {
                firstX = x;
                firstY = y;
                first = false;
            }
            API_VERTEX2F(x, y);
        }
        API_VERTEX2F(firstX, firstY);
        API_END();
    }

    // Playhead outline, drawn under the dots.
    if (playhead >= 0 && playhead < steps && !dim)
    {
        stepPosition(cx, cy, r, steps, static_cast<uint8_t>(playhead), x, y);
        API_COLOR(theme.white);
        API_POINT_SIZE(16 * (dot + 5));
        API_BEGIN(FTPOINTS);
        API_VERTEX2F(x, y);
        API_END();
    }

    API_BEGIN(FTPOINTS);
    for (uint8_t i = 0; i < steps; i++)
    {
        bool pulse = isPulseAt(steps, pulses, rotation, i);
        stepPosition(cx, cy, r, steps, i, x, y);
        API_COLOR(pulse ? pulseColor : restColor);
        API_POINT_SIZE(16 * (pulse ? dot : dot / 2));
        API_VERTEX2F(x, y);
    }
    API_END();
}

void cEuclidRing::update()
{
    if (data == nullptr)
        return;

    const Theme& theme = display.getTheme();

    int16_t cx = posX + width / 2;
    int16_t cy = posY + height / 2;
    int16_t r = (width < height ? width : height) / 2 - 24;

    bool dim = !data->enabled || data->muted;

    drawRing(cx, cy, r, data->steps, data->pulses, data->rotation, data->playhead, dim);

    if (data->hasPending)
    {
        drawRing(cx, cy, r * 2 / 3, data->pendingSteps, data->pendingPulses,
                 data->pendingRotation, -1, true);
    }

    if (data->centerText != nullptr)
    {
        API_COLOR(dim ? theme.inactiveLabelsColors[0] : theme.activeLabelsColors[0]);
        API_CMD_TEXT(cx, cy - (data->centerText2 ? 12 : 0), fontHandle, OPT_CENTER,
                     data->centerText);
    }
    if (data->centerText2 != nullptr)
    {
        API_COLOR(theme.frameColors[0]);
        API_CMD_TEXT(cx, cy + 18, smallFontHandle, OPT_CENTER, data->centerText2);
    }
}
