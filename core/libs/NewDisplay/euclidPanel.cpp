#include "FT812.h"

#include "display.h"
#include "euclidPanelControl.h"

namespace
{
// Indices into fonts[], matching controlStyleFont1..4.
constexpr uint8_t kFontSmall = 1; // 17 regular
constexpr uint8_t kFontBold = 2;  // 14 bold
constexpr uint8_t kFontLarge = 3; // 17 bold

constexpr int16_t kRowH = 38;
constexpr int16_t kPad = 12;
constexpr int16_t kHeaderH = 44;
constexpr int16_t kCellH = 54;
constexpr int16_t kHelpLineH = 24;
constexpr int16_t kMeterAreaH = 40; // above the list, so A-D don't look like channel keys
} // namespace

cEuclidPanel::cEuclidPanel(strControlProperties* properties)
  : cDisplayControl(controlIdEuclidPanel, 1)
{
    colorsCount = 0;
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

    posX = properties->x;
    posY = properties->y;
    width = properties->w;
    height = properties->h;
    data = static_cast<strEuclidPanelData*>(properties->data);
    setStyle(properties->style);
}

cEuclidPanel::~cEuclidPanel()
{
}

void cEuclidPanel::setStyle(uint32_t style)
{
    this->style = style;
}

void cEuclidPanel::setText(const char* text)
{
    this->text = text;
}

void cEuclidPanel::setValue(int value)
{
    this->value = value;
}

void cEuclidPanel::setColors(const uint32_t* colors)
{
    this->colors = colors;
}

void cEuclidPanel::setData(void* data)
{
    this->data = static_cast<strEuclidPanelData*>(data);
}

void cEuclidPanel::drawOutline(int16_t x, int16_t y, int16_t w, int16_t h, uint32_t color)
{
    API_COLOR(color);
    API_LINE_WIDTH(16);
    API_BEGIN(LINE_STRIP);
    API_VERTEX2F(x, y);
    API_VERTEX2F(x + w, y);
    API_VERTEX2F(x + w, y + h);
    API_VERTEX2F(x, y + h);
    API_VERTEX2F(x, y);
    API_END();
}

void cEuclidPanel::fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint32_t color)
{
    API_COLOR(color);
    API_LINE_WIDTH(16);
    API_BEGIN(RECTS);
    API_VERTEX2F(x, y);
    API_VERTEX2F(x + w, y + h);
    API_END();
}

void cEuclidPanel::drawList()
{
    const Theme& theme = display.getTheme();
    const uint8_t rows = data->rows < strEuclidPanelData::kMaxRows ? data->rows
                                                                   : strEuclidPanelData::kMaxRows;
    const int16_t meterH = data->meterCount > 0 ? kMeterAreaH : 0;
    const int16_t listHeight = height - meterH;
    const int16_t listY = posY + meterH;
    const uint8_t visible = static_cast<uint8_t>(listHeight / kRowH);

    // Scroll just far enough to keep the focused row on screen.
    if (data->focusRow >= 0)
    {
        if (data->focusRow < data->firstRow)
            data->firstRow = static_cast<uint8_t>(data->focusRow);
        else if (data->focusRow >= data->firstRow + visible)
            data->firstRow = static_cast<uint8_t>(data->focusRow - visible + 1);
    }
    if (rows <= visible)
        data->firstRow = 0;
    else if (data->firstRow > rows - visible)
        data->firstRow = static_cast<uint8_t>(rows - visible);

    for (uint8_t row = 0; row < visible && data->firstRow + row < rows; row++)
    {
        const uint8_t i = static_cast<uint8_t>(data->firstRow + row);
        int16_t y = listY + row * kRowH;
        int16_t cy = y + kRowH / 2;

        if (i == data->focusRow)
            drawOutline(posX, y + 2, width, kRowH - 4, theme.frameColors[0]);

        bool dim = data->dimRows & (1ul << i);
        API_COLOR(dim ? theme.inactiveLabelsColors[0] : theme.activeLabelsColors[0]);
        if (data->names[i] != nullptr)
            API_CMD_TEXT(posX + kPad, cy, fonts[kFontBold].handle, OPT_CENTERY, data->names[i]);
        if (data->stagedRows & (1ul << i))
            API_COLOR(theme.orange);
        if (data->values[i] != nullptr)
            API_CMD_TEXT(posX + width - kPad, cy, fonts[kFontBold].handle, OPT_CENTERY | OPT_RIGHTX,
                         data->values[i]);
    }

    drawMeters(posY);

    // Small marks at the right edge when there are more rows above or below.
    if (rows > visible)
    {
        API_COLOR(theme.inactiveLabelsColors[0]);
        API_POINT_SIZE(16 * 3);
        API_BEGIN(FTPOINTS);
        if (data->firstRow > 0)
            API_VERTEX2F(posX + width + 4, listY + 6);
        if (data->firstRow + visible < rows)
            API_VERTEX2F(posX + width + 4, listY + visible * kRowH - 6);
        API_END();
    }
}

// One short bar per modulator, labelled A-D: dim when off, outlined when
// it's the one being edited.
void cEuclidPanel::drawMeters(int16_t y)
{
    if (data->meterCount == 0)
        return;
    const Theme& theme = display.getTheme();
    const uint8_t n = data->meterCount < strEuclidPanelData::kMaxMeters ? data->meterCount
                                                                        : strEuclidPanelData::kMaxMeters;
    const int16_t cellW = width / n;
    const int16_t barH = 14;
    const int16_t labelW = 20;

    for (uint8_t i = 0; i < n; i++)
    {
        const int16_t x = posX + i * cellW;
        const int16_t barX = x + labelW;
        const int16_t barW = cellW - labelW - 10;
        const int16_t barY = y + 10;

        char label[2] = { static_cast<char>('A' + i), 0 };
        API_COLOR(data->meterActive[i] ? theme.activeLabelsColors[0] : theme.inactiveLabelsColors[0]);
        API_CMD_TEXT(x + 4, barY + barH / 2, fonts[kFontBold].handle, OPT_CENTERY, label);

        fillRect(barX, barY, barW, barH, theme.frameColors[1]);
        if (i == data->meterSelected)
            drawOutline(barX - 2, barY - 2, barW + 4, barH + 4, theme.frameColors[0]);
        if (!data->meterActive[i])
            continue;

        int16_t v = data->meterValue[i];
        if (v > 1000)
            v = 1000;
        if (v < -1000)
            v = -1000;
        if (data->meterBipolar[i])
        {
            const int16_t mid = barX + barW / 2;
            const int16_t len = static_cast<int16_t>(static_cast<int32_t>(barW / 2) * (v < 0 ? -v : v) / 1000);
            if (len > 0)
                fillRect(v < 0 ? mid - len : mid, barY, len, barH, theme.activeLabelsColors[0]);
            API_COLOR(theme.inactiveLabelsColors[0]);
            API_LINE_WIDTH(16);
            API_BEGIN(LINES);
            API_VERTEX2F(mid, barY - 2);
            API_VERTEX2F(mid, barY + barH + 2);
            API_END();
        }
        else
        {
            const int16_t len = static_cast<int16_t>(static_cast<int32_t>(barW) * (v < 0 ? 0 : v) / 1000);
            if (len > 0)
                fillRect(barX, barY, len, barH, theme.activeLabelsColors[0]);
        }
    }
}

void cEuclidPanel::drawGrid()
{
    const Theme& theme = display.getTheme();
    const uint8_t columns = data->columns > 0 ? data->columns : 4;
    const uint8_t cells = data->cells < strEuclidPanelData::kMaxCells ? data->cells
                                                                      : strEuclidPanelData::kMaxCells;
    const int16_t cellW = width / columns;

    if (data->header != nullptr)
    {
        API_COLOR(theme.activeLabelsColors[0]);
        API_CMD_TEXT(posX + kPad, posY + kHeaderH / 2, fonts[kFontLarge].handle, OPT_CENTERY,
                     data->header);
    }

    const int16_t gridY = posY + kHeaderH;

    for (uint8_t i = 0; i < cells; i++)
    {
        int16_t x = posX + (i % columns) * cellW;
        int16_t y = gridY + (i / columns) * kCellH;
        bool used = i < data->usedCells;

        if (used && i == data->playingCell)
            fillRect(x + 3, y + 5, cellW - 6, kCellH - 10, theme.frameColors[1]);
        if (i == data->cursorCell)
            drawOutline(x + 2, y + 4, cellW - 4, kCellH - 8, theme.frameColors[0]);

        const char* t = used && data->cellText[i] != nullptr ? data->cellText[i] : "-";
        bool dim = !used || (data->dimCells & (1ul << i));
        API_COLOR(dim ? theme.inactiveLabelsColors[0] : theme.activeLabelsColors[0]);
        API_CMD_TEXT(x + cellW / 2, y + kCellH / 2, fonts[kFontLarge].handle, OPT_CENTER, t);
    }

    const int16_t rows = (cells + columns - 1) / columns;
    int16_t helpY = gridY + rows * kCellH + 16;
    API_COLOR(theme.activeLabelsColors[0]);
    for (uint8_t i = 0; i < strEuclidPanelData::kMaxHelpLines; i++)
    {
        if (data->help[i] == nullptr)
            continue;
        API_CMD_TEXT(posX + kPad, helpY, fonts[kFontSmall].handle, 0, data->help[i]);
        helpY += kHelpLineH;
    }
}

void cEuclidPanel::update()
{
    if (data == nullptr)
        return;

    if (data->mode == strEuclidPanelData::Mode::list)
        drawList();
    else if (data->mode == strEuclidPanelData::Mode::lanes)
        drawLanes();
    else
        drawGrid();
}

// One line per channel: its name, then a cell per step (pulses filled,
// bright while the channel is heard), the step playing outlined in orange
// and the selected channel's line framed. Steps are the same width on every
// line, so channels of the same division line up in time.
void cEuclidPanel::drawLanes()
{
    const Theme& theme = display.getTheme();
    const int16_t labelW = 92;
    const int16_t laneH = height / strEuclidPanelData::kLanes;
    const int16_t cellW = (width - labelW - 4) / 32;
    for (uint8_t i = 0; i < strEuclidPanelData::kLanes; i++)
    {
        const strEuclidPanelData::LaneData& l = data->lanes[i];
        const int16_t y = posY + i * laneH;
        const int16_t cy = y + laneH / 2;
        if (i == data->laneSelected)
            drawOutline(posX, y + 2, width, laneH - 4, theme.frameColors[0]);
        API_COLOR(l.on ? theme.activeLabelsColors[0] : theme.inactiveLabelsColors[0]);
        if (l.name != nullptr)
            API_CMD_TEXT(posX + 8, cy, fonts[kFontBold].handle, OPT_CENTERY, l.name);

        const int16_t cellH = laneH - 18;
        for (uint8_t s = 0; s < l.steps && s < 32; s++)
        {
            const int16_t x = posX + labelW + s * cellW;
            const bool pulse = (l.pulses >> s) & 1u;
            if (pulse)
                fillRect(x + 1, y + 9, cellW - 3, cellH,
                         l.on ? theme.activeLabelsColors[0] : theme.inactiveLabelsColors[0]);
            else
                fillRect(x + 1, y + 9, cellW - 3, cellH, theme.frameColors[1]);
            if (s == l.current)
                drawOutline(x - 1, y + 7, cellW + 1, cellH + 4, theme.orange);
        }
    }
}
