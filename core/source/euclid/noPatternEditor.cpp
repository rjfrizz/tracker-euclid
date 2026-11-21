#include "noPatternEditor.h"

strTrackerPattern cPatternEditor::trackerPattern;
cPatternEditor patternEditor;

uint8_t isMultiSelection()
{
    return 0;
}

uint8_t isMultiSelectionOnOneLine()
{
    return 0;
}

int16_t getActualStep()
{
    return 0;
}

int16_t getActualTrack()
{
    return 0;
}

uint8_t isEditMode()
{
    return 0;
}

void sendSelection() {}
void sendCopySelection() {}
void sendPasteSelection() {}
void changeSelectedStepValue(int16_t) {}
