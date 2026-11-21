// Host-side tests for the harmony guess. Not part of the firmware build.
//   c++ -std=c++17 -I core/source core/tests/euclid/euclidHarmonyTest.cpp \
//       core/source/euclid/euclidHarmony.cpp -o /tmp/euclidHarmonyTest

#include "euclid/euclidHarmony.h"

#include <cstdio>
#include <initializer_list>

using namespace euclid;

static int failures = 0;
#define CHECK(cond)                                                                                \
    do                                                                                             \
    {                                                                                              \
        if (!(cond))                                                                               \
        {                                                                                          \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                            \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static HarmonyGuess guess(std::initializer_list<int> notes)
{
    float w[12] = {};
    for (int n : notes)
        w[n % 12] += 1.0f;
    return guessHarmony(w);
}

int main()
{
    HarmonyGuess g = guess({ 60, 64, 67 }); // C E G
    CHECK(g.known && g.root == 0 && !g.minor);
    g = guess({ 57, 60, 64, 57, 45 }); // A C E, A doubled
    CHECK(g.known && g.root == 9 && g.minor);
    g = guess({ 65, 69, 72, 62, 53 }); // F A C, F doubled, a passing D: still F
    CHECK(g.known && g.root == 5 && !g.minor);
    g = guess({ 60 }); // one note: too little
    CHECK(!g.known);
    g = guess({ 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71 }); // everything: no chord
    CHECK(!g.known);
    if (failures == 0)
        std::printf("all euclid harmony tests passed\n");
    return failures == 0 ? 0 : 1;
}
