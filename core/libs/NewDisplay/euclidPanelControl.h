#ifndef LIBS_NEWDISPLAY_EUCLIDPANELCONTROL_H_
#define LIBS_NEWDISPLAY_EUCLIDPANELCONTROL_H_

#include "displayControls.h"

// Right-hand panel of the EUCLID home screen, drawn as one control so the
// screen stays well inside the display's control budget. Shows either a
// list of name/value rows (settings) or a grid of cells (the note list).
struct strEuclidPanelData
{
    static constexpr uint8_t kMaxRows = 20;
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
    uint32_t dimRows = 0;    // bit per row: setting doesn't apply right now
    uint32_t stagedRows = 0; // bit per row: edited, waiting for Rec
    // The list scrolls to keep the focused row in view; firstRow is the
    // top visible row and is kept up to date by the control.
    uint8_t firstRow = 0;

    // Meters along the bottom of a list (the Mod page): one per modulator,
    // -1000..1000 (centred bar) or 0..1000 (from the left).
    static constexpr uint8_t kMaxMeters = 4;
    uint8_t meterCount = 0;
    int8_t meterSelected = -1;
    bool meterActive[kMaxMeters] = {};
    bool meterBipolar[kMaxMeters] = {};
    int16_t meterValue[kMaxMeters] = {};

    // Grid mode.
    const char* header = nullptr;
    uint8_t columns = 4;
    uint8_t cells = 0;      // slots drawn
    uint8_t usedCells = 0;  // slots holding a value; the rest show a dash
    uint16_t dimCells = 0;  // bit per cell: draw its text dimmed
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
    void drawMeters(int16_t y);
    void drawOutline(int16_t x, int16_t y, int16_t w, int16_t h, uint32_t color);
    void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint32_t color);

    strEuclidPanelData* data = nullptr;
};

#endif /* LIBS_NEWDISPLAY_EUCLIDPANELCONTROL_H_ */
