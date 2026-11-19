#ifndef INTERFACE_EUCLIDPARAMS_H_
#define INTERFACE_EUCLIDPARAMS_H_

#include "core/modulesBase.h"
#include "euclid/euclidEngine.h"
#include "euclidPanelControl.h"
#include "euclidRingControl.h"

// EUCLID home screen: the selected channel's ring on the left, the 8
// channels along the bottom over the screen keys, and on the right the
// channel's settings (Params key), its note list (Note key) or the saved
// sets (File key). On the sets view the screen keys become soft keys.
class cEuclidParams : public ModuleBase
{
  public:
    enum Param : uint8_t
    {
        paramName = 0,
        paramSteps,
        paramPulses,
        paramRotation,
        paramDivision,
        paramMode,
        paramChord,
        paramChordChance,
        paramNoteOrder,
        paramKey,
        paramOwnRoot,
        paramOwnScale,
        paramOctave,
        paramRange,
        paramTranspose,
        paramVelocity,
        paramGate,
        paramChance,
        paramMidiChannel,
        paramEnabled,
        paramCount
    };

    enum class View : uint8_t
    {
        params,
        notes,
        sets,
        global,
        mod,
        shift,
        synth,
    };

    // The Shift page's settings rows (Fx 2 again switches to them).
    enum ShiftRow : uint8_t
    {
        shiftRowOn = 0,
        shiftRowLength,
        shiftRowAdvance,
        shiftRowUnits,
        shiftRowCount
    };

    // Rows of the Mod page, for the modulator being edited.
    // Only the rows that apply to the modulator are shown (modVisible).
    enum ModRow : uint8_t
    {
        modRowDest = 0,
        modRowShape,
        modRowAmount,
        modRowRate,
        modRowPhase,
        modRowTarget,
        modRowSteps,
        modRowPulses,
        modRowRotation,
        modRowDivision,
        modRowAutoRotate,
        modRowLength,
        modRowCc,
        modRowCount
    };

    // rowValue() for a row without a value.
    static constexpr int kNoValue = -10000;

    bool isShiftSettings() const { return view == View::shift && shiftSettings; }
    bool isListView() const
    {
        return view == View::params || view == View::global || view == View::mod || view == View::synth ||
               isShiftSettings();
    }
    uint8_t focusedRow() const
    {
        if (view == View::shift)
            return selectedShiftRow;
        if (view == View::synth)
            return selectedSynthRow;
        return view == View::global ? selectedGlobal : (view == View::mod ? selectedModRow : selectedParam);
    }
    uint8_t listRowCount() const
    {
        if (view == View::global)
            return globalCount;
        if (view == View::mod)
            return modVisibleCount;
        if (isShiftSettings())
            return shiftRowCount;
        if (view == View::synth)
            return synthRowCount();
        return view == View::params ? static_cast<uint8_t>(paramCount) : 0;
    }

    enum GlobalParam : uint8_t
    {
        globalTempo = 0,
        globalRoot,
        globalScale,
        globalLaunch,
        globalFreeze,
        globalLayer,
        globalCount
    };

    enum class SetAction : uint8_t
    {
        none,
        save,
        load,
    };

    static constexpr uint8_t kNoteColumns = 4;
    static constexpr uint8_t kNoteSlots = euclid::kMaxNotes;

    virtual void update();
    virtual void start(uint32_t options);
    virtual void stop();

    virtual void initDisplayControls();
    virtual void destroyDisplayControls() {}

    cEuclidParams() {}
    ~cEuclidParams() {}

    void setDefaultScreenFunct();
    void showDefaultScreen();
    void setView(View v);

    void refreshRing();
    void refreshParams();
    void refreshParam(uint8_t param);
    void refreshChannels();
    void refreshFrame();
    void refreshTitle();
    void refreshNotes();
    void refreshSets();
    void refreshGlobal();
    void refreshMod();
    void refreshPads();
    uint32_t paramDimMask() const;
    uint32_t stagedRowMask() const;

    // Pads as a selector for the row in focus.
    struct PadMap
    {
        enum Kind : uint8_t
        {
            none,
            list,     // pad n = the n-th choice
            keyboard, // column of 12 = note (or key offset), any row
            level,    // all 48 pads spread across the value range
        } kind;
        uint8_t count;
        int minValue;
        int maxValue;
    };
    PadMap padMapFor(uint8_t row) const;
    int rowValue(uint8_t row, bool live) const;
    void setRowValue(uint8_t row, int value);
    int valueToPad(const PadMap& m, int value) const;
    int padToValue(const PadMap& m, uint8_t pad) const;
    bool padSelect(uint8_t pad);
    void stopPadBlinks();
    void blinkPad(uint8_t pad, uint8_t level);
    uint64_t blinkingPads = 0;

    // Rec commits staged changes; Rec held + a channel key commits one.
    void commitAll();
    void commitChannel(uint8_t ch);
    void refreshAfterCommit();

    // Freeze (Shift + Rec): the last bars played become a new pattern.
    void requestFreeze();
    void updateFreeze();
    void showMessage(const char* text);
    bool freezePending = false;
    uint32_t freezeEnd = 0; // engine play time the window ends at
    char message[48] = "";
    uint32_t messageUntil = 0; // ms
    bool recHeld = false;
    bool recCommittedChannel = false;
    uint16_t lastPendingMask = 0;

    void selectChannel(uint8_t ch);
    void moveFocus(int8_t delta);
    void moveNoteCursor(int8_t delta);
    void changeParam(int16_t delta);
    void toggleMute(uint8_t ch);

    // Note view editing.
    void addNote(uint8_t note);
    void transposeNote(int16_t delta);
    void deleteNote();
    void duplicateNote();
    void clearNotes();
    // Pads held together on the Note page make one chord entry.
    uint8_t heldEntryPads = 0;
    char noteCellText[kNoteSlots][8];

    // Sets view.
    void moveSetCursor(int8_t delta);
    void setSoftKey(uint8_t key);

    // Global view.
    void changeGlobal(int16_t delta);

    // Mod view (modEuclidParams.cpp).
    PadMap modPadMap(uint8_t row) const;
    int modRowValue(uint8_t row, bool live) const;
    void setModRowValue(uint8_t row, int value);
    void changeMod(int16_t delta);
    uint32_t modDimMask() const;
    // Synth view (synthEuclidParams.cpp).
    void refreshSynth();
    uint8_t synthRowCount() const;
    int synthParamAt(uint8_t row) const;
    PadMap synthPadMap(uint8_t row) const;
    int synthRowValue(uint8_t row) const;
    void setSynthRowValue(uint8_t row, int value);
    void changeSynth(int16_t delta);
    uint8_t selectedSynthRow = 0;
    uint8_t synthGroup[8] = {};
    char synthText[strEuclidPanelData::kMaxRows][20];
    bool synthWasSending = false;

    // Shift view (shiftEuclidParams.cpp).
    void refreshShift();
    PadMap shiftPadMap(uint8_t row) const;
    int shiftRowValue(uint8_t row, bool live) const;
    void setShiftRowValue(uint8_t row, int value);
    void changeShift(int16_t delta);
    void moveShiftCursor(int8_t delta);
    void clearShiftStep(bool all);
    bool shiftSettings = false; // the settings list rather than the steps
    uint8_t selectedShiftRow = shiftRowOn;
    uint8_t shiftCursor = 0;
    int8_t lastShiftPlayed = -2;
    char shiftCellText[euclid::kShiftSteps][6];
    char shiftText[shiftRowCount][16];

    void buildModRows();
    uint8_t modRowId(uint8_t index) const
    {
        return index < modVisibleCount ? modVisible[index] : static_cast<uint8_t>(modRowCount);
    }
    void updateModMeters(bool force);
    uint8_t selectedMod = 0;    // 0-3: Mod A-D
    uint8_t selectedModRow = 0; // index into modVisible
    uint8_t modVisible[modRowCount];
    uint8_t modVisibleCount = 1;
    char modText[modRowCount][16]; // by visible index
    uint8_t lastModRotation = 0xFF;
    uint32_t lastMeterMs = 0;

    View view = View::params;
    View viewBeforeSets = View::params; // where Back on Sets or Global returns to
    uint8_t selectedChannel = 0;
    uint8_t selectedParam = paramPulses;
    uint8_t noteCursor = 0;
    bool replaceNextNote = false;
    uint8_t setCursor = 0; // 0-7 = slots 1-8
    uint8_t selectedGlobal = globalRoot;
    SetAction armedAction = SetAction::none;
    uint32_t lastChangeCount = 0;
    int lastShownTempo = -1;
    bool lastKeyPending = false;

    hControl ring = -1;
    hControl panel = -1;
    hControl channelLabel[8];
    hControl bgLabel = -1;

    strEuclidRingData ringData;
    strEuclidPanelData panelData;

    char ringText[16];
    char ringText2[32];
    char valueText[paramCount][12];
    char globalText[globalCount][12];
    char noteHeaderText[40];
    char setText[8][8];
    char setStatus[40] = ""; // last save/load result, shown until the next action
    char channelText[8][12]; // "8 Counter"
    char channelText2[8][12];
    char titleText[48];

    int8_t lastPlayhead = -2;
    bool lastPending = false;
    bool lastRunning = false;
    int8_t lastNextNote = -1;

    // Pads held for audition, so the note-off matches the note-on.
    int16_t heldPadNote[48];
    uint8_t heldPadChannel[48];
};

extern cEuclidParams euclidParams;

#endif /* INTERFACE_EUCLIDPARAMS_H_ */
