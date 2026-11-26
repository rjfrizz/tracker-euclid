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
        chain,
        lines, // all channels as lines of steps (Pattern)
        perform, // freeze and morph (the Performance key)
    };

    // The Shift page's settings rows (Fx 2 again switches to them).
    enum ShiftRow : uint8_t
    {
        shiftRowLane = 0, // which lane the page shows: Pitch, Mode, Division, Order, Chord
        shiftRowOn,
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
        modRowDelay,
        modRowFade,
        modRowCount
    };

    // rowValue() for a row without a value.
    static constexpr int kNoValue = -10000;

    bool isShiftSettings() const { return view == View::shift && shiftSettings; }
    // The Note page's settings list (Note again): the Params page's note
    // settings, row by row.
    static constexpr uint8_t kNoteParamCount = 10;
    static const uint8_t kNoteParams[kNoteParamCount];
    bool notesSettings = false;
    bool isNoteSettings() const { return view == View::notes && notesSettings; }
    uint8_t paramForRow(uint8_t row) const { return isNoteSettings() && row < kNoteParamCount ? kNoteParams[row] : row; }
    uint8_t noteRowOf(uint8_t param) const
    {
        for (uint8_t i = 0; i < kNoteParamCount; i++)
            if (kNoteParams[i] == param)
                return i;
        return 0;
    }
    void refreshNoteSettings();
    bool isListView() const
    {
        return view == View::params || view == View::global || view == View::mod || view == View::synth ||
               view == View::chain || view == View::perform || isShiftSettings() || isNoteSettings();
    }
    uint8_t focusedRow() const
    {
        if (view == View::shift)
            return selectedShiftRow;
        if (view == View::synth)
            return selectedSynthRow;
        if (view == View::chain)
            return selectedChainRow;
        if (view == View::perform)
            return selectedPerformRow;
        if (isNoteSettings())
            return noteRowOf(selectedParam);
        return view == View::global ? selectedGlobal : (view == View::mod ? selectedModRow : selectedParam);
    }
    uint8_t listRowCount() const
    {
        if (view == View::global)
            return globalCount;
        if (view == View::mod)
            return modVisibleCount;
        if (isShiftSettings())
            return shiftRows();
        if (view == View::synth)
            return synthRowCount();
        if (view == View::chain)
            return chainRowCount();
        if (view == View::perform)
            return performRowCount();
        if (isNoteSettings())
            return kNoteParamCount;
        return view == View::params ? static_cast<uint8_t>(paramCount) : 0;
    }

    enum GlobalParam : uint8_t
    {
        globalTempo = 0,
        globalRoot,
        globalScale,
        globalLaunch,
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

    // Shift + Delete, pressed twice: the selected channel (Params page) or
    // all of them (Pattern page) back to defaults, keeping names and where
    // their notes go. Scenes, sets and the chain stay.
    void requestClear(bool all);
    void clearChannel(uint8_t ch);
    int8_t armedClear = -1; // the channel waiting for a second press, 8 for all
    uint32_t armedClearUntil = 0;

    void showMessage(const char* text);
    void updateMessage(); // puts the normal title back when one runs out
    char message[48] = "";
    uint32_t messageUntil = 0; // ms
    // A message too long for the title scrolls once to its end.
    static constexpr uint8_t kTitleChars = 26; // "Scene B from the next beat" just fits
    uint8_t messageScroll = 0;
    uint32_t messageScrollNext = 0; // ms
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
    void showSynthHint();
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
    uint32_t lastSynthEvents = 0;
    // The Synth page's patch list (File): the selected patch, and the first
    // one on screen.
    // Lines view (Pattern): every channel's rhythm and where it's playing.
    void refreshLines();
    uint32_t linesState() const; // changes when anything the lines show does
    void changeLines(int16_t delta, bool steps); // pulses, or steps with Shift
    void rotateLines(int delta);
    void orderLines(int delta); // the SEQ note order
    uint32_t lastLinesState = 0;
    uint32_t lastLinesMs = 0;
    char lineNames[8][12];
    char lineInfo[8][20];
    char lineShift[8][24];
    char lineNote[8][8];
    // The Pattern page's harmony guess: the notes heard lately, fading.
    void updateHarmony();
    uint32_t chainWarned = 0; // the chain step last reminded about
    float harmonyWeight[12] = {};
    uint32_t harmonyCount[8] = {};
    uint32_t harmonyMs = 0;
    int8_t harmonyRoot = -1; // -1: no guess
    bool harmonyMinor = false;

    // Chain view (chainEuclidParams.cpp).
    void refreshChain();
    // The Perform page (performEuclidParams.cpp).
    uint8_t performRowCount() const;
    void performKey(uint8_t key);
    void nextPerformTab();
    bool performToolOn(uint8_t tool) const;
    void changePerform(int16_t delta);
    void movePerformRow(int delta);
    void refreshPerform();
    uint8_t selectedPerformRow = 0;
    bool lastPerformFrozen = false;
    int lastPerformMorph = 0;
    char performText[13][48];
    // The tools (a tab and a key each): Freeze, Walk, Arp, Scale, Chord,
    // Pulses. Walk, Arp, Scale, Chord and Pulses on (bit per tool; Freeze
    // is the engine's).
    uint8_t performTab = 0;
    uint8_t performOn = 0;
    bool performArpUpDown = false;
    bool performScaleUpDown = false;
    uint8_t performChord = 1;  // euclid::ChordType, triad to start
    uint8_t performPulses = 2; // Pulses adds
    // Progression: which (presets, then the user ones) and the chord edited.
    uint8_t performProg = 0;
    uint8_t performProgSlot = 0;
    uint8_t lastPerformProgSlot = 0xFF;
    bool lastPerformProg = false;
    uint16_t lastActiveKey = 0xFFFF; // root and scale playing, for the title
    bool editPerformProg(); // the progression to edit: a user one (a preset is copied first)
    // Walk, Arp, Scale, Chord and Pulses: the channels each reaches (bit per
    // channel, by tool; Freeze's is the engine's).
    uint8_t performMask[7] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    void applyPerformMode();
    void setPerformTool(uint8_t tool, bool on);
    char performNames[8][16];
    uint8_t chainRowCount() const;
    void changeChain(int16_t delta);
    void moveChainRow(int delta);
    void moveChainField(int delta);
    void insertChainStep();
    void deleteChainStep();
    uint8_t selectedChainRow = 0;
    uint8_t chainField = 0; // 0 set, 1 scene, 2 bars, 3 repeats
    int8_t lastChainPlaying = -1;
    uint32_t lastChainBar = 0;

    // Sampler channels are named after their sample (force: just chosen;
    // otherwise only if the name is still one this gave it).
    void nameSamplerChannels(int onlyChannel, bool force);
    char sampleNamed[8][12] = {};

    bool synthPatches = false;
    uint8_t patchCursor = 0;
    uint8_t patchFirst = 0;
    void openPatches(bool open);
    void movePatchCursor(int delta);
    void savePatch();
    void loadPatch();
    void deletePatch();

    // Shift view (shiftEuclidParams.cpp).
    void refreshShift();
    PadMap shiftPadMap(uint8_t row) const;
    int shiftRowValue(uint8_t row, bool live) const;
    void setShiftRowValue(uint8_t row, int value);
    void changeShift(int16_t delta);
    void moveShiftCursor(int8_t delta);
    void clearShiftStep(bool all);
    bool shiftSettings = false; // the settings list rather than the steps
    uint8_t shiftLane = 0;      // 0 the pitch lane, 1 + euclid::LaneKind for the others
    uint8_t shiftRows() const { return shiftLane == 0 ? shiftRowCount : shiftRowCount - 1; } // Units: pitch only
    // The selected lane, whichever it is.
    bool shiftLaneOn() const;
    uint8_t shiftLaneLength() const;
    bool shiftStepUsed(uint8_t step) const; // not zero (pitch) or none
    int8_t shiftPlayedStep() const;
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
    // The Master page's small line view, where the ring usually is.
    strEuclidPanelData miniData;
    hControl miniLines = -1;
    void fillLanes(strEuclidPanelData& d);
    void refreshMiniLines();

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
    uint32_t lastNoteCount = 0; // generated modes: the note last lit
    int8_t lastNoteShift = 0;   // the Shift step the notes were shown with
    char poolCellText[32][6];

    // Pads held for audition, so the note-off matches the note-on.
    int16_t heldPadNote[48];
    uint8_t heldPadChannel[48];
};

extern cEuclidParams euclidParams;

#endif /* INTERFACE_EUCLIDPARAMS_H_ */
