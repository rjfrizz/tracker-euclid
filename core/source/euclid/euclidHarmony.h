#pragma once

#include <stdint.h>

// A broad guess at the harmony from the notes heard lately: the major or
// minor triad that best fits a weighted count of pitch classes (recent notes
// counting more). Deliberately rough: no sevenths, no inversions, nothing
// when there's too little to go on.
//
// Platform independent, so it's tested on a host (core/tests/euclid/).

namespace euclid
{

struct HarmonyGuess
{
    bool known = false;
    uint8_t root = 0; // 0-11, C = 0
    bool minor = false;
};

// weight: how much each pitch class has sounded lately (any scale).
HarmonyGuess guessHarmony(const float weight[12]);

} // namespace euclid
