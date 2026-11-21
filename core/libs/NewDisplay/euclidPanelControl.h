#ifndef LIBS_NEWDISPLAY_EUCLIDPANELCONTROL_H_
#define LIBS_NEWDISPLAY_EUCLIDPANELCONTROL_H_

#include "displayControls.h"

// Right-hand panel of the EUCLID home screen, drawn as one control so the
// screen stays well inside the display's control budget. Shows either a
// list of name/value rows (settings) or a grid of cells (the note list).
struct strEuclidPanelData
{
    static constexpr uint8_t kMaxRows = 28;
    static constexpr uint8_t kMaxCells = 32;
    static constexpr uint8_t kMaxHelpLines = 4;

    enum class Mode : uint8_t
    {
        list,
        grid,
        lanes,  // every channel's rhythm as a line of steps (the Pattern key)
        blocks, // the chain, a block a step (the Song key)
    } mode = Mode::list;

    // Blocks mode: the chain's On and At-the-end switches across the top,
    // then a row a step: its scene's coloured block (with the set), bars and
    // repeats; the step playing shows how far through it is.
    static constexpr uint8_t kMaxBlocks = 24;
    struct BlockData
    {
        uint8_t set = 0;   // 0 = stay
        uint8_t scene = 0; // 0 = the set's own, 1-4 = A-D
        uint8_t bars = 4;
        uint8_t repeats = 1;
        bool playing = false;
        uint8_t repeat = 0; // while playing: which repeat, and the bar in it
        uint8_t bar = 0;
    } blocks[kMaxBlocks];
    uint8_t blockCount = 0;
    int8_t blockSelected = -1; // a step, or -1 / -2 for the On / At-the-end switch
    uint8_t blockField = 0;    // 0 set, 1 scene, 2 bars, 3 repeats
    uint8_t blockFirst = 0;    // the top row on screen (kept by the control)
    bool chainOn = false;
    bool chainLoop = true;

    // A page's own colour for its bar and the selected row's frame (0: none).
    uint32_t accent = 0;

    // Lanes mode: a line per channel, a cell per step, pulses filled.
    static constexpr uint8_t kLanes = 8;
    struct LaneData
    {
        uint8_t steps = 0;   // as shown: the edit waiting for Rec, if any
        uint32_t pulses = 0; // bit per step
        uint32_t byHand = 0; // bit per step: changed on the pads (added or muted)
        bool pending = false; // edited, not playing yet: drawn in orange
        int8_t current = -1; // the step playing
        bool on = false;     // enabled and not muted
        const char* name = nullptr;
        const char* info = nullptr; // "5/8", under the name
        // The modulators: -1000..1000 (bipolar) or 0..1000.
        // The Shift lanes: which are on (bit per lane: pitch, mode, division,
        // order, chord), and the values playing now.
        uint8_t shiftOn = 0;
        const char* shift = nullptr;
        const char* note = nullptr; // the note it played last ("C4", "C4+2" for a chord)
        uint8_t modActive = 0;  // bit per modulator
        uint8_t modBipolar = 0; // bit per modulator
        int16_t mod[4] = {};
    } lanes[kLanes];
    int8_t laneSelected = -1;
    bool lanesCompact = false; // the Master page's small copy: names and steps only

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
    uint32_t dimCells = 0;  // bit per cell: draw its text dimmed
    uint32_t shiftCells = 0; // bit per cell: shown as Shift moves it (light blue)
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
    void drawLanes();
    void drawBlocks();
    void drawOutline(int16_t x, int16_t y, int16_t w, int16_t h, uint32_t color);
    void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint32_t color);

    strEuclidPanelData* data = nullptr;
};

#endif /* LIBS_NEWDISPLAY_EUCLIDPANELCONTROL_H_ */
