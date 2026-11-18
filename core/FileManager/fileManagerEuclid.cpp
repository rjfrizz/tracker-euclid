// EUCLID: writing frozen patterns into the workspace.

#include "mtSequencer.h"
#include "mtStructs.h"

#include "fileManager.h"
#include "fileManagerDefs.h"

#include <stdio.h>

uint8_t cFileManager::findFreePatternSlot()
{
    for (int i = 1; i <= PATTERN_INDEX_MAX; i++)
    {
        if (i == mtProject.values.actualPattern)
            continue; // may not have reached its file yet
        if (changesFlags.pattern[i])
            continue;
        if (mtProject.values.allPatternsBitmask[i - 1] || mtProject.values.allPatternsBitmaskMidi[i - 1])
            continue;
        if (getPatternPosInSong(static_cast<uint8_t>(i)).has_value())
            continue;
        if (!isPatternEmpty(static_cast<uint8_t>(i)))
            continue;
        return static_cast<uint8_t>(i);
    }
    return 0;
}

bool cFileManager::writeFrozenPattern(uint8_t index, Sequencer::strPattern* pattern)
{
    if (index == 0 || pattern == nullptr)
        return false;

    char path[PATCH_SIZE]{ 0 };
    snprintf(path, PATCH_SIZE, cWorkspacePatternFileFormat, index);
    if (!writePatternFile(path, pattern))
        return false;

    // Like a saved pattern: the Song screen shows which tracks it uses, and
    // the project counts as changed.
    updatePatternBitmask(index, pattern);
    mtProject.values.projectNotSavedFlag |= 1;
    changesFlags.project = 1;
    return true;
}
