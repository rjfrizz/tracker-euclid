// Host-side tests for the chain. Not part of the firmware build.
//   c++ -std=c++17 -I core/source core/tests/euclid/euclidChainTest.cpp \
//       core/source/euclid/euclidChain.cpp core/source/euclid/euclidSets.cpp \
//       core/source/euclid/euclidEngine.cpp -o /tmp/euclidChainTest

#include "euclid/euclidChain.h"

#include <cstdio>

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

static Chain threeSteps()
{
    Chain c;
    c.count = 3;
    c.steps[0] = { 1, 0, 8 };
    c.steps[1] = { 0, 2, 4 };
    c.steps[2] = { 3, 1, 2 };
    return c;
}

static void testStepAt()
{
    Chain c = threeSteps();
    CHECK(c.totalBars() == 14);
    uint32_t start = 99;
    CHECK(chainStepAt(c, 0, &start) == 0 && start == 0);
    CHECK(chainStepAt(c, 7, &start) == 0 && start == 0);
    CHECK(chainStepAt(c, 8, &start) == 1 && start == 8);
    CHECK(chainStepAt(c, 12, &start) == 2 && start == 12);
    // Looping: bar 14 is the first step again, starting at 14.
    CHECK(chainStepAt(c, 14, &start) == 0 && start == 14);
    CHECK(chainStepAt(c, 26, &start) == 2 && start == 26);
    // Not looping: done after the last step.
    c.loop = false;
    CHECK(chainStepAt(c, 13) == 2);
    CHECK(chainStepAt(c, 14) == -1);
}

static void testFile()
{
    Chain c = threeSteps();
    c.on = true;
    c.loop = false;
    uint8_t buf[kChainFileSize];
    CHECK(serializeChain(c, buf, sizeof(buf)) == kChainFileSize);
    Chain d;
    CHECK(deserializeChain(d, buf, sizeof(buf)));
    CHECK(d.on && !d.loop && d.count == 3);
    CHECK(d.steps[1].set == 0 && d.steps[1].scene == 2 && d.steps[1].bars == 4);
    CHECK(d.steps[2].set == 3 && d.steps[2].scene == 1 && d.steps[2].bars == 2);
    buf[10] ^= 1;
    CHECK(!deserializeChain(d, buf, sizeof(buf)));
    CHECK(!deserializeChain(d, buf, sizeof(buf) - 1));
}

int main()
{
    testStepAt();
    testFile();
    if (failures == 0)
        std::printf("all euclid chain tests passed\n");
    return failures == 0 ? 0 : 1;
}
