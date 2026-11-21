#pragma once

// EUCLID has no stock Pattern editor. A few stock files (the sequencer's
// selection editing, MIDI recording, popups) still mention it; this keeps
// them building with nothing to edit: no selection, no edit mode, track 0.

#include "displayControls.h"
#include "trackerControl.h"

#include <stdint.h>

uint8_t isMultiSelection();
uint8_t isMultiSelectionOnOneLine();
int16_t getActualStep();
int16_t getActualTrack();
uint8_t isEditMode();
void sendSelection();
void sendCopySelection();
void sendPasteSelection();
void changeSelectedStepValue(int16_t value);

class cPatternEditor
{
  public:
    void moveCursorByStepParallel() {}
    void refreshPatternFromOutsideModule() {}
    void refreshEditStateFromOutsideModule() {}
    void setPreviousUsedFx(uint8_t) {}
    uint8_t getPreviousUsedFx() { return 0; }
    void setPreviousUsedFxValue(uint8_t) {}
    uint8_t getPreviousUsedFxValue() { return 0; }
    bool isSelectionOrCursorInMidiTracks() { return false; }
    static strTrackerPattern trackerPattern;
    uint8_t editParam = 0;
    hControl patternControl = 0;
};

extern cPatternEditor patternEditor;
