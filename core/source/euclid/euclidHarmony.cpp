#include "euclidHarmony.h"

namespace euclid
{

HarmonyGuess guessHarmony(const float weight[12])
{
    float total = 0;
    for (int i = 0; i < 12; i++)
        total += weight[i];
    HarmonyGuess best;
    if (total < 1.5f)
        return best; // too little heard to say

    float bestScore = -1e9f;
    for (int root = 0; root < 12; root++)
        for (int minor = 0; minor < 2; minor++)
        {
            const int third = (root + (minor ? 3 : 4)) % 12;
            const int fifth = (root + 7) % 12;
            const float inChord = weight[root] * 1.2f + weight[third] + weight[fifth] * 0.8f;
            const float outside = total - weight[root] - weight[third] - weight[fifth];
            const float score = inChord - 0.6f * outside;
            if (score > bestScore)
            {
                bestScore = score;
                best.root = static_cast<uint8_t>(root);
                best.minor = minor != 0;
            }
        }
    // Only when the chord's tones are most of what's heard.
    best.known = bestScore > 0.3f * total;
    return best;
}

} // namespace euclid
