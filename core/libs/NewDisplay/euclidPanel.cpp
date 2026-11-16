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

    for (uint8_t i = 0; i < rows; i++)
    {
        int16_t y = posY + i * kRowH;
        int16_t cy = y + kRowH / 2;

        if (i == data->focusRow)
            drawOutline(posX, y + 2, width, kRowH - 4, theme.frameColors[0]);

        API_COLOR(theme.activeLabelsColors[0]);
        if (data->names[i] != nullptr)
            API_CMD_TEXT(posX + kPad, cy, fonts[kFontBold].handle, OPT_CENTERY, data->names[i]);
        if (data->values[i] != nullptr)
            API_CMD_TEXT(posX + width - kPad, cy, fonts[kFontBold].handle, OPT_CENTERY | OPT_RIGHTX,
                         data->values[i]);
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
        bool dim = !used || (data->dimCells & (1u << i));
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
    else
        drawGrid();
}
