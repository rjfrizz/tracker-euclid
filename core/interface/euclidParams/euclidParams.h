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
        paramSteps = 0,
        paramPulses,
        paramRotation,
        paramDivision,
        paramNoteOrder,
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

    // Sets view.
    void moveSetCursor(int8_t delta);
    void setSoftKey(uint8_t key);

    View view = View::params;
    View viewBeforeSets = View::params;
    uint8_t selectedChannel = 0;
    uint8_t selectedParam = paramPulses;
    uint8_t noteCursor = 0;
    bool replaceNextNote = false;
    uint8_t setCursor = 0; // 0-7 = slots 1-8
    SetAction armedAction = SetAction::none;
    uint32_t lastChangeCount = 0;

    hControl ring = -1;
    hControl panel = -1;
    hControl channelLabel[8];
    hControl bgLabel = -1;

    strEuclidRingData ringData;
    strEuclidPanelData panelData;

    char ringText[16];
    char ringText2[32];
    char valueText[paramCount][12];
    char noteHeaderText[40];
    char setText[8][8];
    char setStatus[40] = ""; // last save/load result, shown until the next action
    char channelText[8][6];
    char channelText2[8][12];
    char titleText[24];

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
