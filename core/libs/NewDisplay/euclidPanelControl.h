#ifndef LIBS_NEWDISPLAY_EUCLIDPANELCONTROL_H_
#define LIBS_NEWDISPLAY_EUCLIDPANELCONTROL_H_

#include "displayControls.h"

// Right-hand panel of the EUCLID home screen, drawn as one control so the
// screen stays well inside the display's control budget. Shows either a
// list of name/value rows (settings) or a grid of cells (the note list).
struct strEuclidPanelData
{
    static constexpr uint8_t kMaxRows = 12;
    static constexpr uint8_t kMaxCells = 16;
    static constexpr uint8_t kMaxHelpLines = 4;

    enum class Mode : uint8_t
    {
        list,
        grid,
    } mode = Mode::list;

    // List mode.
    uint8_t rows = 0;
    const char* names[kMaxRows] = {};
    const char* values[kMaxRows] = {};
    int8_t focusRow = -1;

    // Grid mode.
    const char* header = nullptr;
    uint8_t columns = 4;
    uint8_t cells = 0;      // slots drawn
    uint8_t usedCells = 0;  // slots holding a value; the rest show a dash
    const char* cellText[kMaxCells] = {};
    int8_t cursorCell = -1;
    int8_t playingCell = -1;
    const char* help[kMaxHelpLines] = {};
};

class cEuclidPanel : public cDisplayControl
{
  public:
    cEuclidPanel(strControlProperties* properties = nullptr);
    virtual ~cEuclidPanel();

    virtual void update();

    virtual void setStyle(uint32_t style);
    virtual void setText(const char* text);
    virtual void setValue(int value);
    virtual void setColors(const uint32_t* colors);
    virtual void setData(void* data);

  private:
    void drawList();
    void drawGrid();
    void drawOutline(int16_t x, int16_t y, int16_t w, int16_t h, uint32_t color);
    void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint32_t color);

    strEuclidPanelData* data = nullptr;
};

#endif /* LIBS_NEWDISPLAY_EUCLIDPANELCONTROL_H_ */
